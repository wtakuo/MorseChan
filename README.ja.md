# MorseChan — モールス符号で話す StackChan

M5Stack の StackChan (K151, CoreS3) が、既存アプリ [MorseComm](../MorseComm) と
ESP-NOW でモールス符号のやりとりをして、表情・首の動き・LED で反応する展示用アプリです。

- 来場者は MorseComm (Nesso N1 / M5StickS3 / StickC Plus2 / Cardputer など) で打つだけ。
  **MorseComm 側の変更は不要**です
- StackChan の返事は MorseComm の通常の受信として音が鳴り、画面に復号表示されます
- 対話は生成 AI を使わないルールベース (インターネット不要)
- 打ち間違いに強い認識 (`HEL LO` → HELLO、`HOWAREYOU` → HOW ARE YOU など)
- **胴体のない CoreS3 SE (+ Base M5GO Bottom3) でも動きます** (表情・LED・音。首は動かない)。
  同じファームウェアが起動時に StackChan の胴体の有無を判別します (下の「対応ハードウェア」)

English: [README.md](README.md)　変更履歴: [CHANGELOG.ja.md](CHANGELOG.ja.md)

## 動作環境 (動作確認したバージョン)

| 項目 | バージョン |
|---|---|
| arduino-cli | 1.5.1 (macOS) |
| ボードパッケージ | `m5stack:esp32` 3.3.10 (ESP-IDF 5.5 系) |
| FQBN | `m5stack:esp32:m5stack_cores3` (既定のオプション: 16MB, QSPI PSRAM, USB CDC On Boot) |
| M5StackChan | 1.1.1 (依存: IRremoteESP8266 2.9.0, M5Unit-NFC 0.1.2) |
| M5Stack_Avatar | 0.10.0 |
| M5Unified / M5GFX | 0.2.29 / 0.2.36 |
| 相手の MorseComm | 0.1.0 (Nesso N1 で確認) |

## 対応ハードウェア

| | StackChan (K151) | CoreS3 SE + Base M5GO Bottom3 |
|---|---|---|
| 判別 (起動時) | M5Unified が StackChan / CoreS3 と判定し、胴体の IO エキスパンダ (I2C 0x6F) が応答 | M5Unified が CoreS3 SE と判定 (胴体なし) |
| 首の動き | サーボ (M5StackChan) | なし (首をかしげる動作は顔の傾きだけ) |
| LED | 胴体の 12 個 (M5StackChan) | ボトムの WS2812 10 個、**G5** (M5Unified の LED ドライバ) |
| 頭のタッチ | あり | なし |
| 顔・音・通信・対話・設定画面・居眠り・電池表示 | あり | あり (電池はボトムの 500mAh を CoreS3 の電源 IC で読む) |

- 起動メッセージと `show` に判別結果が出ます (`StackChan` / `CoreS3 SE (no body)`)。
  胴体がないとき、設定画面の MOTION は `NO BODY` と表示されます
- Bottom3 の LED のピンは資料 (M5-Bus の 9 番 "RGB") からは決められず (CoreS3 では 9 番は G35 = 画面の D/C)、
  **実機で G5 であることを確認**しました。Bottom3 が付いていない CoreS3 SE でも動作に支障はありません
- 胴体のない普通の CoreS3 でも、胴体の判別に約 1.5 秒かかるだけで同じように動きます (未確認)

## ビルドと書き込み

`sketch.yaml` に上記のバージョンを固定したプロファイルが 2 つあります: `stackchan` (StackChan) と
`cores3` (胴体のない CoreS3 SE / CoreS3)。中身は同じファームウェアで、ビルドディレクトリを分けるためと
対象を明示するために分けています (arduino-cli のプロファイルにはビルドオプションを持たせられないので、
機種の違いは実行時に判別しています)。

```sh
# 初回のみ: ボードパッケージの URL を登録してインストール
arduino-cli config add board_manager.additional_urls \
    https://static-cdn.m5stack.com/resource/arduino/package_m5stack_index.json
arduino-cli core update-index
arduino-cli core install m5stack:esp32@3.3.10
arduino-cli lib install M5StackChan@1.1.1 M5Stack_Avatar@0.10.0 \
    M5Unified@0.2.29 M5GFX@0.2.36 IRremoteESP8266@2.9.0 M5Unit-NFC@0.1.2

# ポートの確認 (StackChan は /dev/cu.usbmodem* として見える)
arduino-cli board list

# ビルドして書き込み (StackChan)
arduino-cli compile --profile stackchan --build-path build/stackchan \
    -u -p /dev/cu.usbmodemXXXX .
# CoreS3 SE (+ M5GO Bottom3)
arduino-cli compile --profile cores3 --build-path build/cores3 \
    -u -p /dev/cu.usbmodemXXXX .

# FQBN を直接指定する場合
arduino-cli compile -b m5stack:esp32:m5stack_cores3 --build-path build/stackchan .
arduino-cli upload  -b m5stack:esp32:m5stack_cores3 --build-path build/stackchan \
    -p /dev/cu.usbmodemXXXX .
```

注意: 書き込むと StackChan の出荷時ファームウェア (AI エージェントなど) は消えます。
戻すときは M5Burner を使ってください。

シリアル (115200bps, USB CDC):
`tools/ser.sh /dev/cu.usbmodemXXXX 3 "show" "stats"` でコマンドを送って出力を読めます
(`arduino-cli monitor -p PORT` でも可)。

## 使い方

1. StackChan の電源を入れる (起動時に首が正面・少し上向きの待機姿勢に動きます)
2. StackChan のチャンネルを決める (下の「本体での設定」。既定 CH1)。来場者どうしの通信と分けたいときは
   CH1 以外にする
3. 来場者の MorseComm を StackChan と **同じチャンネル** (ESP-NOW) にする
4. MorseComm で `HI` などを打つ。打ち終えて約 2.5 秒 (endgap) 経つと 1 つのメッセージとして確定し、
   StackChan が反応して返事を打ち返します

対話の例 (要件書 4.3 の流れ):

```
StackChan: HI          (長い間だれも打たないときの呼びかけ。既定 180 秒後、続けて最大 2 回)
来場者    : HI
StackChan: ENJOY?      (笑顔。呼びかけへの返事なのでいきなり質問)
来場者    : YES
StackChan: FB!         (大喜び、首を左右に振る、黄色の LED)
```

返事は無線の略語とふつうの言葉の 2 通りがあり、毎回ランダムに選びます
(例: `FB` / `GOOD`、`73` / `BYE`、`TU` / `THANKS`)。

その他の反応 (`dialog_rules.h`):

| 入力 | 反応 | 返事 |
|---|---|---|
| HELLO / HI / HEY | 笑顔・お辞儀・緑 → 続けて質問 | HI / HELLO |
| GM / GOOD MORNING | 笑顔・お辞儀 → 続けて質問 | GM / MORNING |
| GA, GE (GOOD AFTERNOON / EVENING) | 笑顔・お辞儀 → 続けて質問 | GA / HELLO, GE / EVENING |
| HOW ARE YOU / HRU | 笑顔・うなずき | FINE / GOOD |
| FINE / GOOD / FB | 笑顔・うなずき | FB / GOOD |
| BAD / NOT GOOD / TIRED | 残念 | OH / OH NO |
| YES | 大喜び・うなずき | OK / GREAT |
| NO | 残念・首を振る・紫 | OH |
| OK | 笑顔・うなずき | FB / GOOD |
| SOS | 驚き (!)・見回す・赤の点滅 | SOS? / HELP? |
| 73 / BYE / CU / GN | 笑顔・お辞儀 | 73 / BYE |
| TNX / THANKS / TU | 笑顔・お辞儀 | TU / THANKS |
| WHO ARE YOU / NAME | 笑顔・うなずき | STACKCHAN |
| CQ | 大喜び | DE STACKCHAN K |
| CUTE / KAWAII / LOVE | 大喜び | TU / THANKS |
| わからない・判断できない | 困惑 (?)・首をかしげる・青 | ? / WHAT? |

StackChan の質問 (順番に使う): `ENJOY?` `FUN?` `HOW ARE YOU?` `OK?`。
肯定の答えへの返事は `FB!` / `GOOD!` (または `FB` / `GOOD`)、否定には `OH`。
YES / OK / FINE / GOOD などは肯定、NO / BAD などは否定の答えとして扱います。
質問待ちのあいだも HELLO・SOS・73 などには普通に反応します。
答えが 30 秒来なければ少し残念な顔をして待機に戻ります (送信はしない)。
わからない返事のときは `?` を返し、同じ質問の答えを待ち続けます。

### 表情・動作・LED

- 表情は M5Stack-Avatar の標準の顔。Avatar の表情 (Happy / Sad / Doubt / Neutral) に、
  小さな記号を重ねて区別します: 驚き = `!` と大きく開いた口、困惑 = `?`、考え中 (受信中) = 点々 `...`、
  大喜び = きらきら + 口がぱくぱく、残念 = 汗のしずく
- 首: 左右を見る、見回す、うなずく、首をかしげる (顔の傾き + 首)、首を振る、お辞儀、喜びの揺れ。
  どれも 1〜3 秒で待機姿勢に戻ります。動作の間には 1.5 秒以上の間を空けます (待機中の見回し)
- LED (12 個): 喜び = 緑、大喜び = 黄、残念 = 紫、困惑 = 青、SOS = 赤の点滅、
  受信中 = 来場者のキーに合わせて水色、送信中 = 自分のキーに合わせて橙、待機 = ごく弱い光
- スピーカー: 自分が送る符号を 700Hz (MorseComm のサイドトーンと同じ) で鳴らします。
  受信した符号も鳴らすには `rxtone on`
- 画面をタップすると状態表示 (チャンネル・LINK・対話の状態・最後に認識した語) を表示/非表示。
  頭のタッチセンサーをタップすると喜んでうなずきます (送信はしない)

### 居眠り (NO SIGNAL のとき)

StackChan のチャンネルに MorseComm が 1 台もいない状態 (ハートビートが 3 秒届かない) が
DOZE 秒 (既定 30 秒) 続くと居眠りします: 目を閉じて `Z z z`、首が少し下がり、LED がゆっくり青く呼吸。
同じチャンネルに MorseComm の電源が入る (またはチャンネルを合わせる) と、ハッと起きます
(`!` と首がぴょこっと上がる → 笑顔)。頭のタッチでも起きます。
居眠り中は見回しと呼びかけをしません (チャンネルにだれもいないときは呼びかけない)。
MorseComm は電池運用で 3 分操作がないとスリープするので、その場合も StackChan は居眠りします。

### MorseChan を 2 台使うとき

同じチャンネルに MorseChan が 2 台 (例: StackChan と CoreS3 SE) いると、お互いの返事を受信して返事をし合い、
会話が続いてしまいます (MorseComm のプロトコルには MorseChan どうしを見分ける情報がない)。
**MorseChan ごとに別のチャンネルにしてください** (設定画面の CHANNEL)。

### 本体での設定 (PC なしで変更できる)

**画面の下の方 (下約 3 分の 1) から上へスワイプ**すると設定画面になります
(StackChan のデモアプリでホームに戻る操作と同じ。短いタップは状態表示の切り替え)。
シリアルの `menu` でも開閉できます。
項目の行をタップして選び、下の **[◀] [▶]** で値を変え、**[DONE]** で顔に戻ります
(30 秒操作がないときも戻る)。変更はすぐ反映され、**その場で NVS (フラッシュ) に保存**されるので、
次に電源を入れたときもその設定で動きます。音量を変えると確認の音が鳴ります。
画面の上の行には、バージョンと電池残量 (アイコンと %、充電中は `+`) を表示します。

| 項目 | 値 |
|---|---|
| CHANNEL | CH1〜CH8 (MorseComm のチャンネル) |
| VOLUME | 0〜5 |
| SPEED (WPM) | 5〜30 (送信速度) |
| MOTION | OFF / SMALL / NORMAL |
| LED | 0〜3 |
| CALL-OUT | OFF / 60 / 120 / 180 / 300 / 600 秒 |
| DOZE | OFF / 15 / 30 / 60 / 120 / 300 秒 |
| RX TONE | OFF / ON |

設定は電源を切っても残ります (シリアルの `defaults` で既定値に戻る)。

## MorseComm との通信互換性

MorseComm のソースを調査した結果、MorseComm は **文字ではなく「キーの状態 (DOWN/UP) と直近の遷移履歴」**
を送っています。StackChan もこれとまったく同じ方式で送受信します。

- 無線: ESP-NOW、Wi-Fi チャンネル 1 固定、STA モード、**ブロードキャスト** (FF:FF:FF:FF:FF:FF)、暗号化なし
- パケット (13 + 2n バイト, リトルエンディアン): magic 0xC7 / version 1 / **channel (1..8)** / state /
  pktSeq / lastTid / txMs / 履歴数 n / age[n]。キーが DOWN の間と UP 後 0.5 秒は 20ms ごと、
  それ以外は 500ms ごとのハートビート
- 受信: 送信元 MAC ごとにジッタバッファ (150ms) でタイミングを再現し、MorseComm と同じ適応型デコーダで文字にする
- 送信: 返事の文字列を MorseComm の端末モード / 登録メッセージと同じ `StreamKeyer` でキーイング
  (既定 20WPM) し、同じパケットで送る。**復号済みテキストを独自パケットで送ることはしない**ので、
  MorseComm 側では普通の受信として音が鳴り、文字が表示されます
- 流用したファイル: `protocol.h` `jitter.h` `peers.h` `morse_decoder.h` `morse_table.h` `keyer.h`
  `tone_out.h` は MorseComm 0.1.0 から**無変更でコピー**。`config.h` の前半は MorseComm と同じ値
  (変えると通信できなくなる)。`net.cpp` `engine.cpp` `tone_out_m5speaker.cpp` は MorseComm のものを
  StackChan 用に簡略化 (LoRa・キー入力・ロス注入を除去)
- メッセージの区切り: MorseComm には「メッセージの終わり」を表す仕組みがない (連続したキーイング) ので、
  送信元が **endgap (既定 2500ms) の間何も打たない**と 1 つのメッセージとして確定します
- StackChan の出荷時ファームの ESP-NOW 遠隔操作プロトコルとは別物で、使っていません
- 混信への対処: MorseComm と同じく packet の channel (1..8) で分けます。さらに `peer aa:bb:cc:dd:ee:ff`
  で特定の MorseComm 1 台だけを相手にできます (MorseComm の MAC は MorseComm の起動メッセージ /
  StackChan の `log 1` の RXCHR 行に出ます)。メッセージを組み立てている間は最初に打ち始めた局だけを相手にし、
  他の局の文字は無視します (`stats` の ignored)
- デコーダの性質: dit の長さを適応的に推定する MorseComm のデコーダをそのまま使っているので、StackChan が
  理解する文字は、来場者の MorseComm の画面に出る (自分の送信の) 復号結果と同じになります。
  新しい局の最初のメッセージが 10WPM より遅く、間隔のばらつきが大きいと、短点を長点と読み違えることがあります
  (MorseComm と同じ。ホストテストで 11WPM・±15% のとき 48/50 で認識)。2 通目からは速度に追従します

## 入力の正規化と認識 (recognizer.h)

1. 正規化: 大文字に統一、`?` などの記号・略符号 `[SK]` を除去、連続した空白を 1 つに、前後の空白を削除
   (解読できない符号は `#`)。元の受信文字列はログにそのまま残します
2. 照合 (この順に優先):
   - 完全一致: `HOW ARE YOU`
   - 空白を除いて一致: `HEL LO` `HE LLO` `H E L L O` → HELLO、`HOWAREYOU` → HOW ARE YOU
     (**登録した定型句との比較だけに使い**、受信文字列そのものは書き換えない)
   - 語の並びを含む: `HELLO STACKCHAN`、`TNX 73` (SOS > 73/BYE > その他 の優先度)。
     語の途中には一致しない (`I KNOW` は NO にならない)
   - 近似一致 (編集距離 1): 空白を除いて **5 文字以上の定型句だけ** (`HELLP` → HELLO、`THANKZ` → THANKS)。
     YES / NO / OK などの短い語は完全一致のみ
3. 別々の意味の候補が同点なら推測せず「わからない」(`?` と困惑の表情)

`rec TEXT` コマンドで認識結果を確認できます。

## 対話ルールの追加・変更 (dialog_rules.h)

- 返事を変える: `RULES` の該当行の文字列を変える (送れる文字は A-Z, 0-9, `. , ? / = + - ( ) ' : ; " @ ! _ &`)。
  `"FB|GOOD"` のように `|` で区切ると、毎回ランダムにどれか 1 つを送ります
- 同じ意味の言い方を増やす: `PHRASES` に `{Intent::Hello, "HOWDY"}` のように追加 (大文字・空白 1 つ)
- 新しい意味を増やす: `recognizer.h` の `enum class Intent` と `dialog_rules.h` の `intentName`
  に追加し、`PHRASES` と `RULES` に行を足す
- 質問を増やす: `QUESTIONS` に {質問, 肯定の反応, 肯定の返事, 否定の反応, 否定の返事} を追加
- 反応は `{表情, 首の動き, LED}`。名前は `actions.h` (表情 7 種、動き 8 種、LED 6 種)
- `ask = true` の規則は、返事のあとに質問を 1 つします
- 変更後は `make -C test/host test` を実行してください。空白を除くと同じになる定型句が別の意味に
  割り当てられていないか、返事が送れる文字だけか、などを検査します

## 設定

本体の設定画面 (上記) またはシリアルコマンドで変更し、Preferences (名前空間 `morsechan`) に保存されます
(`defaults` で既定値に戻る)。endgap・送受信音の周波数・peer はシリアルからだけ変更できます。

| コマンド | 既定 | 内容 |
|---|---|---|
| `ch N` | 1 | MorseComm のチャンネル (1..8) |
| `wpm N` | 20 | 送信速度 (MorseComm の既定に合わせた) |
| `txhz N` / `rxhz N` | 700 / 600 | 送信音 / 受信音の周波数 (MorseComm のサイドトーン / 受信音と同じ) |
| `rxtone on\|off` | off | 受信した符号も鳴らす |
| `vol N` | 2 | 音量 0..5 (0 = 消音) |
| `peer MAC\|any` | any | 相手を 1 台に限定 |
| `motion 0\|1\|2` | 2 | 首の動き なし / 小さく / 普通 (リアクションの強さ) |
| `led 0..3` | 2 | LED の明るさ |
| `callout N` | 180 | 待機中の呼びかけまでの秒数 (0 = しない) |
| `doze N` | 30 | だれもいないとき居眠りするまでの秒数 (0 = しない) |

`menu` で設定画面の開閉、`bat` で電池の読み値 (M5.Power と StackChan 本体の INA226)、
`layershot` で記号・状態表示・設定画面のレイヤーを 1 ビットの画像として 16 進で出力 (スクリーンショット用)、
`log 1` でタッチの判定 (`TOUCH swipe-up (x,y)->(x,y) ms`) も表示されます。
| `endgap MS` | 2500 | メッセージが終わったとみなす無音時間 (800..10000) |

`show` は uptime と前回のリセット理由 (reset=poweron / usb / brownout / panic など) も表示します。
その他のコマンド: `show` `stats [on|off]` `log 0|1` `send TEXT` `sim TEXT` (無線なしで受信したことにする)
`rec TEXT` `face NAME` `gesture NAME` `ledtest NAME` `status on|off` `reboot` `help`。

コンパイル時の定数 (`config.h`): 返事までの間 (700ms)、質問までの間 (1.2s)、答えの待ち時間 (30s)、
表情を保つ時間 (4s)、首の可動範囲 (左右 ±45°、上下 0〜45°、待機 15° 上向き)、
動作の最小間隔 (1.5s)、待機中の見回し間隔 (20〜45s) など。

## ソフトウェア構成

```
MorseChan.ino          setup()/loop(): 各モジュールの統合、シリアルコマンド、設定の保存
engine.h/.cpp          1ms 周期の通信タスク (送信キーイング、パケット送受信、再生、デコード、音)
net.h/.cpp             ESP-NOW (MorseComm から流用・簡略化)
shared.h               タスク間の共有データ (Settings, Snapshot, OutItem, 送信待ち文字)
protocol.h jitter.h peers.h morse_decoder.h morse_table.h keyer.h   MorseComm から無変更でコピー
tone_out.h tone_out_m5speaker.cpp                                  トーン出力 (MorseComm から流用)
assembler.h            受信文字をメッセージにまとめる (無音で確定)            ※
recognizer.h           正規化と定型句の照合                                 ※
dialog_rules.h         対話データ (定型句・返事・質問)                        ※
board.h                対象ボードの確認 (CoreS3 系)。StackChan かどうかは robot.cpp が実行時に判別
settings_menu.h        本体の設定画面 (項目、値の変え方、タッチの配置)          ※
touch_gesture.h        画面のタッチをタップ / 上スワイプに分類                  ※
dialog.h               対話の状態機械 (Idle/Receiving/Responding/Asking/WaitingForAnswer) ※
actions.h              抽象コマンド (表情・動き・LED の名前)                   ※
gestures.h             首の動きのキーフレームと安全範囲                        ※
robot.h/.cpp           StackChan の制御 (Avatar の顔、サーボ、LED、タッチ)
config.h board.h version.h sketch.yaml
test/host/             ホスト (Mac) 上のテスト
tools/ser.sh           シリアルにコマンドを送って出力を読む
```
※ Arduino 非依存 (時刻を引数で受け取る) で、ホストでテストできるモジュール。

- 通信タスク (優先度 5) は loop() と別に 1ms 周期で動くので、顔の描画 (Avatar のタスク)、
  サーボ (M5StackChan の motion タスク)、LED (I2C) の処理に妨げられません。
  ESP-NOW の受信コールバックは受信時刻を記録してキューに積むだけです
- 対話エンジンはハードウェアの API を知らず、`Reaction{Happy, Nod, Happy}` や「`HI` を送る」という
  抽象的な Action を出し、MorseChan.ino がそれを robot と engine に渡します
- 記号・状態表示・設定画面は loop() で 1 ビットの画像 (2 枚を交互に使う) に描き、Avatar の描画タスクでは
  顔の部品の 1 つを包む Overlay がそれを顔に重ねるだけです (描画タスクのスタックは 2KB しかなく、
  そこで文字を描くとあふれて再起動した)。画面に書き込むのは常に Avatar のタスクだけです。
  I2C (LED・タッチ) は loop() からだけ使います

## テスト

### ホストテスト (実機不要)

```sh
make -C test/host test
```

正規化・照合 (受け入れ条件 1, 2)、規則表の整合性、メッセージの組み立て、対話の状態機械
(質問と YES/NO・タイムアウト・質問中の特別な入力・送信中に届いたメッセージ・呼びかけ, 条件 4, 5, 7)、
首の動きの安全範囲 (条件 8)、MorseComm のプロトコルコードを通した送受信 (ロス・時計のずれ・人の打鍵の
ばらつきを含む, 条件 3 のロジック部分)、返事のランダムな選択、だれもいないときの呼びかけ停止、
設定画面の値の変え方とタッチの配置を検査します (2367 件)。

### 実機テストの手順

MorseComm を載せた機器 (例: Nesso N1) と StackChan を Mac につなぎます。MorseComm の `term` モードを
使うと、MorseComm の保存設定を変えずにシリアルから文字を送れます (Ctrl-C で終了)。

1. StackChan の `show`、MorseComm の `show` で、どちらも ESP-NOW・同じチャンネルであることを確認。
   StackChan の `stats` で `stations=1` になっていること
2. MorseComm で `term` → `HELLO` と打つ。StackChan のシリアルに `RX "HELLO" ... -> hello (exact)`、
   `TX "HI"`、続いて質問の `TX "..."` が出る。MorseComm に `< HI` と質問が表示される (条件 1, 3)
3. `YES` / `NO` と打つ → 質問に応じた返事 (条件 4)
4. 1 文字ずつ 1 秒以上あけて `H` `E` `L` `L` `O` と打つ → `RX "H E L L O" -> hello (spaces)` (条件 2)
5. `XQZ` など → `?` と困惑の表情 (条件 5)
6. 質問のあと 30 秒何もしない → `STATE WaitingForAnswer -> Idle` (条件 7)
7. 受信・送信の最中に首が動いても `stats` の lost / late / wd が増えないこと (条件 6)
8. `gesture nod` などで各動作を確認し、首が無理な位置に行かないこと (条件 8)。
   `face happy` など、`ledtest alert` などで表情と LED を確認

## 実機での確認状況

### 0.3.0 (2026-11-06, CoreS3 SE + Base M5GO Bottom3 1 台 + Nesso N1 (MorseComm 0.1.0) 1 台, 机上)

確認済み:
- M5Unified が CoreS3 SE と判定し、胴体なしで起動 (`CoreS3 SE (no body)`)。電池 (ボトムの電池) を読める
- ボトムの LED を G5 で駆動して赤・緑・青が正しく光ることをユーザーが確認 (試験用スケッチ)
- Nesso との対話: `HEL LO` → `HELLO` と `ENJOY?`、YES → `GOOD!`、未知語 → `WHAT?`、SOS → `HELP?`、73 → `73`。
  MorseComm の画面に返事が表示された。居眠り (局がいない) と目覚め (局が現れる)
- 描画タスクの残りスタックは 196 バイト (StackChan と同じ)

未確認: CoreS3 SE での表情・LED の色 (MorseChan として)・音量・設定画面の見た目とスワイプ (目視待ち)、
**0.3.0 を StackChan に書き込んでの再確認** (胴体の判別、首の動き、胴体の LED)

### 0.2.0 (2026-11-06): 本体の設定画面、返事のバリエーション、居眠り

確認済み (シリアルのログ):
- 居眠り: StackChan だけ CH2 にすると 30 秒後に `DOZE`、CH1 (Nesso がいる) に戻すとすぐ `WAKE`
- 返事のバリエーション: YES への返事が `GOOD!` (ふつうの言葉) になった。73 / TNX / FINE などにも反応
- `show` に uptime と reset (前回のリセット理由) を追加。長時間運転の観察に使えます

- 居眠り・起きるときの見た目はユーザーが確認済み

不具合と修正 (2026-11-06):
- 最初の版 (長押しで設定画面) では、設定画面が出た直後に**再起動していた** (「一瞬暗くなって顔に戻る」の正体)。
  原因は M5Stack-Avatar の描画タスクのスタック (2KB) のあふれ。同様に、状態表示を出しているときに困惑の `?` を
  出すと再起動した。→ 記号・状態表示・設定画面はすべて loop() 側で 1 ビットの画像に描き、描画タスクでは
  顔の画像に重ねる (OR / コピー) だけにした。描画タスクの残りスタックは顔だけのときと同じ約 200 バイト
  (`show` の draw_stack_free で確認できる)
- 設定画面を開く操作を、長押しから下からのスワイプに変更 (タッチの判定は自前。接触の一瞬の途切れも吸収)

- スワイプでの設定画面の開閉・操作はユーザーが確認済み

### 0.2.1 (2026-11-06)
- 設定画面で値を変えたらすぐ NVS に保存 (以前は DONE か 30 秒後だったので、その前に電源を切ると失われた)。
  再起動後に設定が残ることを確認済み
- ボタンを ◀ ▶ (三角形) に、上の行にバージョンと電池残量。電池は M5.Power (CoreS3 の電源 IC。StackChan 本体の
  電池を読む) を 5 秒ごとに読む。INA226 の電圧と一致することを確認 (4.15V で 100%)。
  表示は `layershot` で取り込んだ画像で確認

### USB 接続についての注意 (2026-11-06 に調査)

PC (Mac) の USB につないだまま首を速く動かすと、**USB シリアルが切れる・応答しなくなる**ことがあります
(書き込みも失敗する)。本体は動き続けます (uptime は途切れず、無線・顔・対話は正常。Nesso からも見えている)。
`motion 0` (サーボを動かさない) では 16 回の動作で一度も起きず、サーボが速く動くとき (喜びの揺れ・起きる動作)
に起きるので、サーボの電流のピークが原因と考えられます (PC の USB ポートの電流制限の可能性もあり。未確定)。
対策としてサーボ速度に上限 (`config.h` の `SERVO_SPEED_MAX` = 500) を設け、40 回で 1 回に減りました
(上限なしでは最初の速い動作で発生)。展示では USB 充電器から給電するので影響はありません。
USB シリアルが応答しなくなったら USB を抜き差ししてください。

**原因の切り分け (0.3.0 の確認時):** 上記は USB ケーブルを StackChan の**足元 (胴体) のコネクタ**につないだときの現象。
**CoreS3 本体の USB ポート**につなぐと、速度の上限 (500) ありで 40 回、上限なし (1000) でも 70 回の速い動作で一度も
切れなかった。胴体側の USB の配線がサーボの近くを通るため、サーボの電流の影響を受けると考えられます。
**開発時は CoreS3 の USB ポートにつなぐのがおすすめ**です。速度の上限 500 は、見た目を確認済みの今の動きとして残しています。

### 0.1.0 (2026-11-06, StackChan 1 台 + Nesso N1 (MorseComm 0.1.0) 1 台, 机上)

確認済み (シリアルのログで確認):
- 起動、ESP-NOW の初期化、Nesso のハートビートの受信 (stations=1, RSSI 約 -27dBm)
- Nesso → StackChan: `HELLO` `HI` `YES` `NO` `OK` `SOS` `73` `XQZ`、1.2 秒間隔の `H E L L O`
  をすべて意図どおり認識。StackChan → Nesso: `HI` `ENJOY?` `FUN?` `OK?` `HOW ARE YOU?` `FB!` `OH` `?`
  `SOS?` `73` が MorseComm の画面に表示された
- 要件書の対話例 (呼びかけ HI → HI → ENJOY? → 答え → FB!)、答えのタイムアウト (30 秒) で Idle に戻ること、
  質問の送信中に届いた 73 への反応
- 約 1 分 10 秒の試験でパケット 1332 個中ロス 2、遅延超過・ウォッチドッグ 0 (首の動作中を含む)

- ユーザーが目視で確認済み: 表情・記号、首の動き (上下の位置を含む)、LED の色、音量、画面タップ・頭のタッチ

未確認: 長時間 (数時間) の連続運転 (ユーザーが観察中)、複数の MorseComm がいる環境、電池での運用

## ライセンス

MIT (LICENSE)。MorseComm から流用したファイルも同じ作者・同じライセンスです。
M5StackChan (MIT, M5Stack)、M5Stack-Avatar (MIT, Shinya Ishikawa) を使用しています。
