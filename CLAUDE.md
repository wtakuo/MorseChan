# CLAUDE.md — MorseChan (StackChan × MorseComm モールス対話)

要件は requirements.md。使い方・構成・互換性の説明は README.ja.md (英語版 README.md。内容を変えたら両方直す)。
このファイルは作業メモ (日本語)。

## 1. 方針 (requirements.md からの要点)

- MorseComm (/Users/takuo/m5/MorseComm, 同じ作者) と**無変更で**通信する。MorseComm 側は変更しない
- 独自プロトコルを作らない。復号済みテキストを独自パケットで送らない
- 対話はルールベース。対話エンジンはハードウェア API に依存しない (actions.h の抽象コマンド)
- 実機で確認していないことを確認済みと書かない

## 2. MorseComm からの流用 (2026-11-06, MorseComm 0.1.0, コミット 492f458 時点)

- 無変更コピー: protocol.h jitter.h peers.h morse_decoder.h morse_table.h keyer.h tone_out.h .clang-format LICENSE tools/ser.sh
  → MorseComm 側でこれらが変わったら、`cmp` で差分を確認してコピーし直す
- config.h の前半 (MorseComm compatibility) は MorseComm の config.h と同じ値。変えると通信できない
- 簡略化して流用: net.cpp (ESP-NOW のみ)、engine.cpp (キー入力なし。StreamKeyer で g_typeBuf を送る)、
  tone_out_m5speaker.cpp (ボードの #if を外しただけ)
- 通信方式の要点: ESP-NOW ブロードキャスト、Wi-Fi ch1、パケット先頭 0xC7/1/channel。状態+遷移履歴を 20ms ごと
  (アイドル 500ms)。受信はジッタバッファ 150ms + ウォッチドッグ。詳細は MorseComm の CLAUDE.md §4
- MorseComm にメッセージの区切りは無い → MorseChan は無音 endgap (既定 2500ms) で確定 (assembler.h)

## 3. 環境

- FQBN `m5stack:esp32:m5stack_cores3` (m5stack:esp32 3.3.10)。`esp32:esp32:m5stack_cores3` もあるが使っていない。
  StackChan も CoreS3 SE も同じ FQBN。プロファイルは stackchan / cores3 (中身は同じ、ビルドディレクトリを分ける)
- ライブラリ: M5StackChan 1.1.1 / M5Stack_Avatar 0.10.0 / M5Unified 0.2.29 / M5GFX 0.2.36 (sketch.yaml の profile stackchan)
- 実機: StackChan 1 台、CoreS3 SE + M5GO Bottom3 1 台と、MorseComm 入りの Nesso N1 1 台を Mac につないで試験する。いずれも /dev/cu.usbmodem* として見え、
  ポート名は接続のたびに変わりうるので `arduino-cli board list` と `show` (MorseChan / MorseComm の起動メッセージ) で見分ける
- Nesso の設定 (2026-11-06 の show): ESP-NOW STRAIGHT 20WPM side 700Hz rx 600Hz vol3。チャンネルはユーザーが変える
  (ch1 → ch2) ので、試験前に Nesso の `show` で確認して MorseChan 側を合わせる。**Nesso の保存設定は変えない**。試験は MorseComm の `term` モード (Ctrl-C で終了) でシリアルから文字を送る
- 2 台同時の試験スクリプトはセッションの scratchpad に duo.py として作った (リポジトリには入れていない)。
  Python で両ポートを raw 115200 で開き、Nesso に "term\r" と文字を書き、両方の出力を時刻付きで表示する
- StackChan のシリアル (USB CDC) はポートを開いたときに再起動することがある

## 4. StackChan ライブラリの調査結果 (M5StackChan 1.1.1, ソースで確認)

- `M5StackChan.begin()` が M5.begin()、タッチセンサー、PY32 IO エキスパンダ (I2C, サーボ電源 pin0 / RGB pin13)、
  サーボ (SCS バス UART1 1Mbps, GPIO6/7)、INA226 を初期化。IO エキスパンダの起動待ちで約 0.6〜1.2 秒かかる
- サーボ: `Motion.move(yaw, pitch, speed)`。単位 0.1 度、speed 0..1000。yaw -1280..1280 (ID1)、pitch 0..900 (ID2, 0..90 度)。
  ゼロ位置は NVS "servo" の zero_pos_1/2 (HomeCalibration 例で設定)。Motion はライブラリ自身のタスク (優先度 10, 20ms 周期) が
  バネのアニメーションで動かす。isMoving / getCurrentAngle は UART を読むので使っていない
- **pitch の 0 が正面なのか下向きなのかは未確認**。例の Dance で 0 = "Look Down"、900 = "Look Up"。MorseChan は待機を
  pitch 150 (15 度上)、お辞儀・うなずきで 0 方向へ下げる (config.h の REST_PITCH)。実機で見て調整すること
- RGB LED 12 個 (0-5 左, 6-11 右): `setRgbColor` + `refreshRgb` / `showRgbColor`。I2C なので loop() からだけ呼ぶ
- 頭のタッチ: `TouchSensor.wasClicked()` (M5StackChan.update() で更新)
- Avatar (0.10.0): 描画タスク (優先度 1) と表情タスク (まばたき・視線・呼吸, 優先度 2) を APP_CPU に作る。
  表情は Happy/Angry/Sad/Doubt/Sleepy/Neutral。標準の顔 (Eye) では Doubt は Neutral と同じ見た目、眉は高さ 0 で見えない。
  → MorseChan は左眉を Overlay (robot.cpp) で包み、Avatar の描画タスク内で記号 (! ? ... きらきら しずく) と状態表示を描く
- スピーカー: M5.Speaker (CoreS3 の AW88298)。MorseComm と同じく DMA を 64x4 に短縮して遅れを減らしている

## 5. タスク構成

- engine (優先度 5, 1ms, 非固定): 送信キーイング・送受信・再生・デコード・トーン。共有は shared.h + portMUX
- loop() (優先度 1): シリアル、assembler、dialog、robot (Avatar の設定、サーボ指令、LED、タッチ)
- Avatar の draw/facial タスク、M5StackChan の motion タスク、M5.Speaker のタスク、Wi-Fi タスク

## 6. 作業ルール (MorseComm に合わせる)

- コメント・文字列は英語、US-ASCII のみ、4 スペース、80 桁 (clang-format。.clang-format の先頭に手順)
- シリアルの改行は CR LF。プロンプト "chan> "
- **手持ちのデバイスの MAC アドレス、USB のポート名やシリアル番号などの実機固有の情報はリポジトリ (CLAUDE.md を含む)
  に書かない** (MorseComm と同じ方針。ユーザー指示 2026-11-06)
- 変更履歴は CHANGELOG.md (英語) / CHANGELOG.ja.md (日本語)。バージョンを上げたら version.h と両方を直す
- パラメータは config.h に集約
- Arduino 非依存部 (assembler / recognizer / dialog / dialog_rules / actions / gestures) を変えたら `make -C test/host test`
- ホストテストは時刻を引数で渡して決定的に動かす

## 7. 作業状況 (2026-11-06)

### 0.3.1 (ユーザー要望, 2026-11-06): 起動画面
- 起動時に 3 秒 (cfg::SPLASH_MS) スプラッシュ。内容はユーザー指定: "MorseChan" / "Version x.y.z" /
  "Takuo Watanabe (JM1TEU)" と "w/ Claude" (2 行に分けた。MorseComm と同じ)。レイアウトは任された:
  FreeSansBold24pt のタイトル、その下に M C のモールス符号の棒 (-- -.-.)、FreeSans12pt の版、9pt の作者
- 設定画面と同じオーバーレイレイヤー (置き換え) で loop() 側から描く (描画タスクで文字を描かない規則のため)。ユーザーが表示を確認済み。
  優先順位は 設定画面 > スプラッシュ > 記号・状態表示。StackChan で layershot により確認、3 秒後に layer: empty
- 0.3.0 は 22d742d でコミット済み

### 0.3.0 (ユーザー要望, 2026-11-06): CoreS3 SE + Base M5GO Bottom3 への移植
- 胴体のない CoreS3 SE (+ M5GO Bottom3)。同じ FQBN (m5stack_cores3) なのでコンパイル時には区別できず、arduino-cli の
  プロファイルは build_properties を持てない (試したら黙って無視された) → **実行時に判別** (robot.cpp の detectBody)。
  M5.begin() は 2 回目以降何もしないので、先に M5.begin() → 判別 → 胴体ありなら M5StackChan.begin()。
  判別: M5.getBoard() が board_M5StackChan (M5GFX が 0x6F の IO エキスパンダを見て判定) なら胴体あり、CoreS3 なら
  0x6F を最大 1.5 秒 scanID (エキスパンダの起動が遅いことがある)、CoreS3 SE (カメラなし) なら胴体なし
- 胴体なし: M5StackChan の関数は一切呼ばない (hasBody で分岐)。LED は M5Unified の LED_Strip_Class + LedBus_RMT
  (`<utility/led/LED_Strip_Class.hpp>` を別に include) で M5.Led に設定、10 個、GRB。サーボの動きは省略 (顔の傾きは残す)
- **Bottom3 の LED は G5** (試験用スケッチで赤緑青をユーザーが確認)。資料の M5-Bus 9 番 "RGB" は CoreS3 では G35 (LCD の
  D/C と共有) なので使わないこと。電池はボトムの電池を M5.Power (AXP2101) で読める (4.165V)
- 実機 (CoreS3 SE + Nesso) で対話・居眠りを確認。CoreS3 SE での表情・LED・音量・設定画面はユーザーが確認済み
- StackChan に 0.3.0 を書いて確認: "StackChan" と判別、INA226 も読める、保存済みの設定 (CH2, doze 60) は残った
- **USB が切れる問題の原因を切り分け (ユーザーの気づき):** それまで USB は StackChan の足元 (胴体) のコネクタにつないでいた。
  CoreS3 本体の USB ポートにつなぐと、上限 500 で 40 回・上限なし (1000, scratchpad にコピーしたスケッチでビルド) で 70 回の
  速い動作で切断 0。→ 胴体側の USB 配線がサーボの電流の影響を受けていた。SERVO_SPEED_MAX = 500 はユーザーの判断でそのまま。
  開発時は CoreS3 の USB ポートにつなぐ
- 注意: 未コミットの変更があるときに `git checkout <file>` で一時的な編集を戻そうとしないこと (他の未コミットの変更まで消える。
  自動モードでも拒否された)。一時的な試験はスケッチを scratchpad にコピーして行う
- 注意: 同じチャンネルに MorseChan が 2 台いると返事をし合う。ユーザーの判断で「別チャンネル運用」(README に注意を記載)。
  試験中に StackChan (CH2) と Nesso (CH2) が同じチャンネルにいたことがある → 試験前に `stats` の stations を確認する
- Nesso はユーザーが CH2 に変えていた (2026-11-06 の時点)。Nesso の設定は変えずに、MorseChan 側のチャンネルを合わせて試験した

### 0.2.0 (ユーザー要望, 2026-11-06)
- 本体の設定画面 (当初は長押し、のちに下からのスワイプで開く。settings_menu.h、描画は robot.cpp の drawMenu)。展示では来場者どうしが
  CH1 を使うので、MorseChan のチャンネルを本体で変えたいという要望。音量・速度・動き・LED・呼びかけ・居眠り・受信音も
- 返事に無線用語とふつうの言葉の両方 (`"FB|GOOD"` のように | 区切り、Dialog が xorshift でランダムに選ぶ。setSeed で
  esp_random を種に)
- 居眠り: 自チャンネルに局がいない (stations == 0) 状態が doze 秒 (既定 30) 続くと Sleepy (目を閉じる + Zzz、首を
  10 度下げる、LED が青く呼吸)。局が現れたら WakeUp (! → Happy)。居眠り中・局なしでは呼びかけしない (Dialog::setPresence)
- ホストテスト 2367 件合格。実機に書き込み済み。居眠り (CH2 にして 30 秒で DOZE、CH1 で WAKE) と返事の
  バリエーション (YES → GOOD!) をシリアルで確認。設定画面・居眠りの見た目はユーザーの目視待ち
- `show` に uptime と reset 理由、Avatar 描画タスクの残りスタック (draw_stack_free) を追加
- **設定画面で再起動する不具合 (修正済み):** 長押しで設定画面を開くと直後に panic 再起動 ("Stack canary watchpoint
  triggered (drawLoop)")。M5Stack-Avatar の drawLoop はスタック 2048 バイト (ライブラリ固定) で、顔だけで残り約 200 バイト。
  そこで文字 (設定画面、状態表示 + Font4 の "?") を描くとあふれた。→ 記号・状態表示・設定画面は loop() で 1 ビットの
  レイヤー (320x240, 2 枚交互) に描き、Overlay::draw は OR / memcpy で顔に重ねるだけ。**描画タスク内で文字や重い描画を
  しないこと**。修正後は全表情・状態表示・設定画面で draw_stack_free 196〜212 (顔だけと同じ)
- 設定画面を開く操作はユーザーの提案で「下から上へスワイプ」に変更 (touch_gesture.h。M5Unified の hold / click は
  指が 8px 動くと flick 扱いになる・接触の途切れで判定がリセットされるので使わない)。`menu` コマンドでも開閉
- **USB シリアルが切れる / 固まる問題 (調査済み、未解決):** Mac につないで首を速く動かすと USB が切れる
  ("Device not configured") か、ポートはあるが無応答になる (書き込みも "No serial data received")。
  本体は動き続ける (uptime 継続、Nesso から見える)。motion 0 では 16 回で発生 0、速い動作 (wiggle / wakeup) で発生
  → サーボの電流ピーク (または Mac のポートの電流制限) と推定。SERVO_SPEED_MAX = 500 で 40 回中 1 回に減少
  (上限なしでは最初の速い動作で発生)。展示は USB 充電器で給電 (ユーザー回答) なので影響なし。
  固まったらユーザーに USB の抜き差しを依頼する。試験用の stress.py / stress2.py はセッションの scratchpad にあった
- 試験の注意: duo.py が途中で落ちると Nesso が term モードのまま残る (シリアルのコマンドが Morse で送られてしまう)。
  Ctrl-C (0x03) を送って抜けること。Nesso の設定は 2026-11-06 の終わりに show で元のままであることを確認済み

### 0.2.1 (ユーザー要望, 2026-11-06)
- 設定画面で値を変えるたびに NVS に保存 (putSettings(s, true))。再起動後に残ることを確認
- ボタンを ◀ ▶ (fillTriangle)。タイトル行にバージョンと電池 (M5.Power.getBatteryLevel / isCharging を loop() で 5 秒ごと)。
  M5.Power の電圧 4145mV と M5StackChan.getBatteryVoltage() (INA226) 4.149V が一致 → CoreS3 の AXP2101 が本体の電池を読んでいる
- `bat`、`layershot` コマンド。layershot の出力 (行番号:16進) を PNG にして画面を確認できる
  (scratchpad の shot.py。リポジトリには入れていない)。USB シリアルは setTxTimeoutMs(0) なので大量出力は行ごとに delay が要る

### 0.1.0

- 全フェーズ (調査、通信、対話エンジン、表情・動作、展示向けの機能) を実装。ホストテスト 2334 件合格
- 実機 (StackChan + Nesso N1) で確認済み: 双方向通信、HELLO/HEL LO/H E L L O/YES/NO/OK/SOS/73/未知語の認識と返事、
  MorseComm 画面での返事の表示、呼びかけ → 質問 → 答えの流れ、答えのタイムアウト、質問中の 73。ロス 2/1332
- ユーザーが目視で確認済み (2026-11-06): 表情・記号・首の動き (上下の位置を含む。REST_PITCH 150 で OK)・LED・音量・タッチ
- 長時間運転はユーザーが観察中
- git リポジトリ (ブランチ main) を 2026-11-06 に作成 (ユーザー指示)。リモートはまだない
