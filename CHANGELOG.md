# Changelog

日本語: [CHANGELOG.ja.md](CHANGELOG.ja.md)

## 0.3.1 (2026-11-06)

- Start-up splash screen for 3 s: "MorseChan", the initials M C in Morse
  code, the version and the author (radio and dialog already run under it)

## 0.3.0 (2026-11-06)

- Runs also on a CoreS3 SE without the StackChan body, e.g. with the Base
  M5GO Bottom3: face, sound and the bottom's 10 WS2812 LEDs (G5); no head
  motion. One firmware for both: the body is detected at start-up
- New build profile `cores3` in sketch.yaml
- The settings screen shows `NO BODY` for MOTION without the body
- README: use a separate channel for each MorseChan (two on one channel keep
  answering each other)
- README: the USB drops during fast head motions happen with the cable in the
  StackChan's foot connector, not in the CoreS3's own USB port (tested); the
  servo speed cap of 500 is kept

## 0.2.1 (2026-11-06)

- Settings changed on the settings screen are saved to NVS immediately
  (before: only on DONE or after 30 s without a touch)
- The [-] [+] buttons of the settings screen are now [◀] [▶]
- The top line of the settings screen shows the battery level (icon and %,
  `+` while charging); the version moved next to the title
- Diagnostic commands `bat` (battery readings) and `layershot` (dumps the
  overlay layer as an image)

## 0.2.0 (2026-11-06)

- On-device settings screen, opened by swiping up from the bottom of the
  screen (channel, volume, speed, motion, LEDs, call-out, dozing, receive
  tone); the serial command `menu` also opens / closes it
- Replies mix ham radio words and plain words, chosen at random (`FB` /
  `GOOD`, `73` / `BYE`, `TU` / `THANKS`, ...)
- Dozing: with no MorseComm on the channel, StackChan dozes off (eyes closed,
  Zzz, head drooping, LEDs breathing) and wakes up with a start when a station
  appears; no call-outs to an empty channel; new setting `doze`
- A short beep when the volume or tone changes
- `show` reports uptime, the last reset reason and the free stack of the face
  drawing task
- Fix: StackChan rebooted when the settings screen was shown, or when the
  status lines and the confused `?` were shown together (stack overflow in the
  Avatar draw task). Marks, status lines and the settings screen are now drawn
  by loop(); the draw task only merges them into the face
- Servo speed limit, since fast head motions sometimes dropped the USB
  connection to the PC

## 0.1.0 (2026-11-06)

- First version. Two-way Morse code with MorseComm over ESP-NOW (MorseComm's
  protocol code reused unchanged)
- Messages ended by silence; normalization tolerant of misplaced spaces and
  phrase matching (`HEL LO` -> HELLO, ...)
- Rule-based dialog (state machine), questions from StackChan and reactions to
  the answers, idle call-out
- Faces (M5Stack-Avatar plus marks), head motions, LEDs, speaker sound of the
  transmitted code
- Serial commands, saved settings, host tests
