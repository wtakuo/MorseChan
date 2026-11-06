# MorseChan — a StackChan that talks in Morse code

An exhibit application for the M5Stack StackChan (K151, CoreS3). StackChan
exchanges Morse code with the existing [MorseComm](../MorseComm) app over
ESP-NOW and reacts with its face, head movements and LEDs.

- Visitors just key on MorseComm (Nesso N1 / M5StickS3 / StickC Plus2 /
  Cardputer ...). **MorseComm needs no changes**
- StackChan's replies are played and decoded by MorseComm like any other
  station
- Rule-based dialog, no generative AI, no Internet
- Tolerant recognition (`HEL LO` -> HELLO, `HOWAREYOU` -> HOW ARE YOU, ...)
- **Also runs on a CoreS3 SE without the robot body (+ Base M5GO Bottom3)**:
  face, LEDs and sound, no head motion. The same firmware detects the body at
  start-up (see "Supported hardware")

日本語: [README.ja.md](README.ja.md) / Changes: [CHANGELOG.md](CHANGELOG.md)

## Environment (versions tested)

| Item | Version |
|---|---|
| arduino-cli | 1.5.1 (macOS) |
| Board package | `m5stack:esp32` 3.3.10 (ESP-IDF 5.5) |
| FQBN | `m5stack:esp32:m5stack_cores3` (default options: 16MB, QSPI PSRAM, USB CDC On Boot) |
| M5StackChan | 1.1.1 (deps: IRremoteESP8266 2.9.0, M5Unit-NFC 0.1.2) |
| M5Stack_Avatar | 0.10.0 |
| M5Unified / M5GFX | 0.2.29 / 0.2.36 |
| Peer MorseComm | 0.1.0 (tested with a Nesso N1) |

## Supported hardware

| | StackChan (K151) | CoreS3 SE + Base M5GO Bottom3 |
|---|---|---|
| Detected (at start-up) | M5Unified reports StackChan / CoreS3 and the body's IO expander (I2C 0x6F) answers | M5Unified reports CoreS3 SE (no body) |
| Head motion | servos (M5StackChan) | none (head tilt = face tilt only) |
| LEDs | 12 in the body (M5StackChan) | 10 WS2812 in the bottom on **G5** (M5Unified LED driver) |
| Head touch | yes | no |
| Face, sound, radio, dialog, settings screen, dozing, battery | yes | yes (the bottom's 500mAh battery, read by the CoreS3 PMIC) |

The start-up message and `show` print the result (`StackChan` / `CoreS3 SE
(no body)`); without the body the MOTION item of the settings screen reads
`NO BODY`. The Bottom3 LED pin could not be determined from the docs (bus pin
9 "RGB" is G35 = LCD D/C on the CoreS3) and was **confirmed as G5 on the
hardware**. A plain CoreS3 without the body should work the same way (the
body check takes about 1.5 s; not tested).

## Build and upload

`sketch.yaml` has two profiles pinning the versions above: `stackchan` and
`cores3` (CoreS3 SE / CoreS3 without the body). They build the same firmware
(arduino-cli profiles cannot carry build options, so the hardware is detected
at run time); separate profiles keep separate build directories and name the
target.

```sh
# Once: add the board package URL and install
arduino-cli config add board_manager.additional_urls \
    https://static-cdn.m5stack.com/resource/arduino/package_m5stack_index.json
arduino-cli core update-index
arduino-cli core install m5stack:esp32@3.3.10
arduino-cli lib install M5StackChan@1.1.1 M5Stack_Avatar@0.10.0 \
    M5Unified@0.2.29 M5GFX@0.2.36 IRremoteESP8266@2.9.0 M5Unit-NFC@0.1.2

arduino-cli board list   # StackChan shows up as /dev/cu.usbmodem*
arduino-cli compile --profile stackchan --build-path build/stackchan \
    -u -p /dev/cu.usbmodemXXXX .
arduino-cli compile --profile cores3 --build-path build/cores3 \
    -u -p /dev/cu.usbmodemXXXX .      # CoreS3 SE (+ M5GO Bottom3)

# Or with the FQBN directly
arduino-cli compile -b m5stack:esp32:m5stack_cores3 --build-path build/stackchan .
arduino-cli upload  -b m5stack:esp32:m5stack_cores3 --build-path build/stackchan \
    -p /dev/cu.usbmodemXXXX .
```

Uploading replaces the StackChan factory firmware (restore it with M5Burner).
Serial: 115200 bps USB CDC; `tools/ser.sh PORT 3 "show" "stats"` sends
commands and prints the output.

## Usage

1. Power on the StackChan (the head moves to its resting pose)
2. Choose StackChan's channel (see "Settings on the device"; default CH1).
   Use another channel than the one visitors use among themselves
3. Set the visitor's MorseComm to the **same channel** (ESP-NOW)
4. Key `HI` etc. on MorseComm. After about 2.5 s of silence (endgap) the
   message is complete and StackChan reacts and keys its reply

Example (requirements sec. 4.3):

```
StackChan: HI       (call-out after a long idle time; default 180 s, max 2)
Visitor  : HI
StackChan: ENJOY?   (smiles; a reply to the call-out goes straight to a question)
Visitor  : YES
StackChan: FB!      (excited, wiggles, yellow LEDs)
```

Replies come in a ham radio word and a plain word, chosen at random
(`FB` / `GOOD`, `73` / `BYE`, `TU` / `THANKS`, ...).

| Input | Reaction | Reply |
|---|---|---|
| HELLO / HI / HEY | happy, bow, green, then a question | HI / HELLO |
| GM / GOOD MORNING | happy, bow, then a question | GM / MORNING |
| GA, GE | happy, bow, then a question | GA / HELLO, GE / EVENING |
| HOW ARE YOU / HRU | happy, nod | FINE / GOOD |
| FINE / GOOD / FB | happy, nod | FB / GOOD |
| BAD / NOT GOOD / TIRED | sad | OH / OH NO |
| YES | excited, nod | OK / GREAT |
| NO | sad, shake head, purple | OH |
| OK | happy, nod | FB / GOOD |
| SOS | surprised (!), look around, red blinking | SOS? / HELP? |
| 73 / BYE / CU / GN | happy, bow | 73 / BYE |
| TNX / THANKS / TU | happy, bow | TU / THANKS |
| WHO ARE YOU / NAME | happy, nod | STACKCHAN |
| CQ | excited | DE STACKCHAN K |
| CUTE / KAWAII / LOVE | excited | TU / THANKS |
| not understood / ambiguous | confused (?), head tilt, blue | ? / WHAT? |

Questions (rotating): `ENJOY?` `FUN?` `HOW ARE YOU?` `OK?`. YES / OK / FINE /
GOOD ... are positive answers, NO / BAD ... negative. HELLO, SOS, 73 etc. still
work while a question is pending. With no answer for 30 s StackChan looks a
little sad and returns to idle (nothing is sent). An answer it does not
understand gets `?` and the question stays pending.

Face: the standard M5Stack-Avatar face plus small marks (`!` surprised, `?`
confused, animated `...` while receiving, sparkles when excited, a drop when
sad). Head: look left/right/around, nod, tilt (face rotated too), shake, bow,
wiggle; each returns to rest within 1-3 s, at least 1.5 s apart. LEDs: green
happy, yellow excited, purple sad, blue confused, red blinking for SOS, cyan
following the visitor's keying, orange following our keying, faint glow idle.
The speaker plays our own keying at 700 Hz (`rxtone on` also plays received
keying). Tap the screen to show/hide the status lines; tap the head touch
sensor for a happy nod.

### Splash screen

At power-on (reset) a splash screen shows "MorseChan", the initials M C in
Morse code, the version and the author for 3 s (`SPLASH_MS` in `config.h`).
Radio and dialog already run underneath.

### Dozing (NO SIGNAL)

When no MorseComm is heard on StackChan's channel for DOZE seconds (default
30), StackChan dozes off: eyes closed, `Z z z`, head drooping a little, LEDs
slowly breathing blue. When a MorseComm appears on the channel (powered on,
or switched to it) StackChan wakes up with a start (`!`, the head pops up,
then a smile). A tap on the head also wakes it. No look-arounds and no
call-outs while nobody is on the channel.

### Two MorseChans

Two MorseChans on the same channel (e.g. the StackChan and a CoreS3 SE) hear
each other's replies and keep answering each other (the MorseComm protocol
cannot tell a MorseChan from a visitor). **Give each MorseChan its own
channel** (CHANNEL on the settings screen).

### Settings on the device (no PC needed)

**Swipe up from the lower part of the screen** to open the settings screen
(like the "home" gesture of the StackChan demo app; a short tap toggles the
status lines; the serial command `menu` also opens / closes it). Tap a row to select it, change it with **[◀] [▶]**, and
**[DONE]** returns to the face (also after 30 s without a touch). Changes take
effect at once and are **saved to NVS (flash) immediately**, so they are used
after the next power-on; changing the volume beeps. The top line shows the
version and the battery level (icon and %, `+` while charging).

| Item | Values |
|---|---|
| CHANNEL | CH1..CH8 (MorseComm channel) |
| VOLUME | 0..5 |
| SPEED (WPM) | 5..30 |
| MOTION | OFF / SMALL / NORMAL |
| LED | 0..3 |
| CALL-OUT | OFF / 60 / 120 / 180 / 300 / 600 s |
| DOZE | OFF / 15 / 30 / 60 / 120 / 300 s |
| RX TONE | OFF / ON |

## Compatibility with MorseComm

MorseComm sends **key state (DOWN/UP) plus a short transition history**, not
characters. MorseChan uses exactly the same scheme:

- ESP-NOW, Wi-Fi channel 1 fixed, STA mode, **broadcast**, no encryption
- Packet (13 + 2n bytes, little endian): magic 0xC7 / version 1 / **channel
  (1..8)** / state / pktSeq / lastTid / txMs / n / age[n]; every 20 ms while
  keying (and 0.5 s after), else a 500 ms heartbeat
- Receive: per-sender jitter buffer (150 ms) and MorseComm's adaptive decoder
- Transmit: the reply is keyed with MorseComm's `StreamKeyer` (as in its
  terminal mode / stored message; 20 WPM default) and sent in the same packets.
  Decoded text is **never** sent in a private packet format
- Copied **unchanged** from MorseComm 0.1.0: `protocol.h` `jitter.h` `peers.h`
  `morse_decoder.h` `morse_table.h` `keyer.h` `tone_out.h`. The first part of
  `config.h` has MorseComm's values (do not change them). `net.cpp`,
  `engine.cpp`, `tone_out_m5speaker.cpp` are simplified copies (no LoRa, key
  input or loss injection)
- End of message: MorseComm has no end-of-message marker, so a message ends
  after **endgap ms (default 2500) of silence** from the sender
- Not related to the ESP-NOW remote-control protocol of the StackChan factory
  firmware
- Interference: channels as in MorseComm; `peer aa:bb:cc:dd:ee:ff` limits
  StackChan to one MorseComm. While a message is being assembled, other
  stations are ignored (`stats` ignored=)
- Because the decoder is MorseComm's, StackChan understands the same text the
  visitor sees decoded on their own MorseComm. A new station's first message
  slower than about 10 WPM with very uneven timing may be misread until the
  decoder adapts (host test: 48/50 at 11 WPM with +-15% timing errors)

## Normalization and recognition (recognizer.h)

Upper case; punctuation and prosigns like `[SK]` removed; spaces collapsed and
trimmed (undecodable characters become `#`). The raw text is kept in the log.
Matching, in order: exact; equal with spaces removed (only compared with
registered phrases); a run of whole words (`HELLO STACKCHAN`, `TNX 73`; never
inside a word); edit distance 1 for phrases of 5+ letters only (YES / NO / OK
must be exact). A tie between different meanings is not guessed (`?`).
`rec TEXT` shows how a text is recognized.

## Changing the dialog rules (dialog_rules.h)

- Change a reply: edit the string in `RULES` (sendable: A-Z, 0-9,
  `. , ? / = + - ( ) ' : ; " @ ! _ &`). `"FB|GOOD"` sends one of the
  variants, chosen at random
- Another way to say something: add `{Intent::Hello, "HOWDY"}` to `PHRASES`
- A new meaning: add it to `enum class Intent` (recognizer.h) and
  `intentName`, then to `PHRASES` and `RULES`
- A new question: add it to `QUESTIONS`
- Reactions are `{emotion, gesture, LED}` (names in `actions.h`); `ask = true`
  asks a question after the reply
- Run `make -C test/host test`: it checks for phrases that collide once spaces
  are removed, unsendable replies, missing rules, etc.

## Settings

The settings screen above or serial commands; saved in Preferences
(namespace `morsechan`), `defaults` restores them. endgap, the tone
frequencies and peer are serial-only.

| Command | Default | Meaning |
|---|---|---|
| `ch N` | 1 | MorseComm channel 1..8 |
| `wpm N` | 20 | transmit speed (MorseComm's default) |
| `txhz N` / `rxhz N` | 700 / 600 | tone of our keying / received keying |
| `rxtone on\|off` | off | also play received keying |
| `vol N` | 2 | volume 0..5 |
| `peer MAC\|any` | any | talk to one MorseComm only |
| `motion 0\|1\|2` | 2 | head motion off / small / normal |
| `led 0..3` | 2 | LED brightness |
| `callout N` | 180 | idle call-out after N s (0 = never) |
| `doze N` | 30 | doze after N s with no station (0 = never) |
| `endgap MS` | 2500 | silence that ends a message (800..10000) |

Other commands: `show` `stats [on|off]` `log 0|1` `send TEXT` `sim TEXT`
(handle TEXT as if received) `rec TEXT` `face NAME` `gesture NAME`
`ledtest NAME` `status on|off` `reboot` `help`. Compile-time timing and servo
limits are in `config.h` (yaw +-45 deg, pitch 0..45 deg, rest 15 deg up).

## Software structure

```
MorseChan.ino      integration, serial commands, settings
engine.h/.cpp      1 ms Morse task: keying, packets, playback, decoding, tone
net.h/.cpp         ESP-NOW (from MorseComm)
shared.h           data shared between the task and loop()
protocol.h jitter.h peers.h morse_decoder.h morse_table.h keyer.h  (MorseComm)
tone_out.h tone_out_m5speaker.cpp                                  (MorseComm)
assembler.h        characters -> messages (silence ends a message)   *
recognizer.h       normalization and phrase matching                 *
dialog_rules.h     phrases, replies, questions                       *
settings_menu.h    on-device settings screen (items, layout)         *
touch_gesture.h    screen touches -> tap / swipe up                  *
dialog.h           dialog state machine                              *
actions.h          abstract commands (emotion, gesture, LED)         *
gestures.h         head keyframes and the safe range                 *
robot.h/.cpp       StackChan: Avatar face, servos, LEDs, touch
config.h board.h version.h sketch.yaml test/host/ tools/ser.sh
```
`*` Arduino-independent, tested on the host.

The Morse task (priority 5, 1 ms) is independent of loop(), the Avatar draw
task and the M5StackChan motion task. The ESP-NOW receive callback only
timestamps and queues packets. The dialog engine knows no hardware API; it
emits abstract actions. Marks, status lines and the settings screen are drawn
by loop() into a 1-bit layer that the Avatar draw task only merges into the
face (its 2 KB stack is too small for drawing text), so only that task writes
to the display; I2C (LEDs, touch) is used from loop()
only.

## Tests

Host: `make -C test/host test` (2367 checks: recognition, rule tables,
assembler, dialog state machine, reply variants, no call-out to an empty
channel, settings screen, gesture limits, end-to-end through the MorseComm
protocol code with loss, clock skew and uneven human keying).

Hardware: connect a MorseComm device and the StackChan; use MorseComm's `term`
mode to key text without changing its saved settings. See README.ja.md
"実機テストの手順" for the step-by-step list (acceptance items 1-8).

## Hardware verification status

0.3.0 (2026-11-06, CoreS3 SE + Base M5GO Bottom3 and a Nesso N1 with
MorseComm 0.1.0, desk): detected as `CoreS3 SE (no body)`, battery readable;
the bottom LEDs on G5 show red / green / blue correctly (user-checked with a
test sketch); dialog with the Nesso (HEL LO, YES, unknown word, SOS, 73) with
the replies shown on MorseComm; dozing and waking; draw task free stack 196
bytes. Not yet checked: faces, LED colors, volume and the settings screen on
the CoreS3 SE by eye, and **0.3.0 on the StackChan** (body detection, head
motion, body LEDs).

0.2.1 (2026-11-06): settings are saved to NVS on every change (before, only
on DONE or after 30 s); ◀ ▶ buttons; battery level (M5.Power, which reads the
StackChan body battery; matches the INA226 reading) in the title line. New
diagnostic commands: `bat` (battery readings), `layershot` (dumps the overlay
layer as hex for screenshots).

0.2.0 fix (2026-11-06): the first version (hold to open the settings
screen) **rebooted** right after showing the screen: the M5Stack-Avatar draw
task has a 2 KB stack and drawing the menu text there overflowed it (so did
the status lines together with the "?" mark). Now all marks, status lines and
the settings screen are drawn by loop() into a 1-bit layer, and the draw task
only merges it into the face; its free stack stays at about 200 bytes, the
same as with the bare face (`show`: draw_stack_free). The screen is now
opened by swiping up (own touch classification, touch_gesture.h). Swiping on
the device is still to be checked by the user.

0.2.0 (2026-11-06): verified from the serial logs: dozing after 30 s with
StackChan alone on CH2 and waking at once back on CH1 (where the Nesso is);
plain-word replies (`GOOD!` to YES). `show` now reports uptime and the last
reset reason, for long runs. Not yet verified: the settings screen and its
touch handling, how dozing / waking looks, the volume preview beep.

USB note: while connected to a Mac, fast head motions sometimes drop or hang
the USB serial connection (uploads fail too) while the firmware keeps running
(uptime continues; radio, face and dialog work). It never happened with
`motion 0`, so the servos' current peaks are the likely cause (the PC port's
current limit is also possible; not confirmed). A servo speed cap
(`SERVO_SPEED_MAX` = 500 in `config.h`) reduced it to 1 in 40 gestures. On a
USB charger (as planned for the exhibition) it does not matter. Replug the USB
cable if the serial port stops answering.

Cause narrowed down (0.3.0): this happened with the USB cable in the
StackChan's **foot (body) connector**. Plugged into the **CoreS3's own USB
port**, 40 fast gestures with the cap and 70 without it (speed 1000) caused no
drop. The body's USB wiring runs near the servos and picks up their current
peaks. **For development, plug into the CoreS3's USB port.** The cap of 500
stays, as the motions were checked by eye with it.

0.1.0 (2026-11-06, 1 StackChan + 1 Nesso N1 with MorseComm 0.1.0, desk):

Verified (serial logs): ESP-NOW link; recognition of HELLO, HI, YES, NO, OK,
SOS, 73, XQZ and `H E L L O` keyed 1.2 s apart; replies HI, ENJOY?, FUN?, OK?,
HOW ARE YOU?, FB!, OH, ?, SOS?, 73 shown on MorseComm; the call-out dialog;
the 30 s answer timeout; 73 during a question; 2 lost / 1332 packets, no late
transitions or watchdogs (including during head motion).

Checked by the user: faces and marks, head motion (including the pitch
positions), LED colors, volume, touch. Not yet verified: long runs (being
observed), several MorseComm stations, battery use.

## License

MIT (LICENSE). Uses M5StackChan (MIT, M5Stack) and M5Stack-Avatar (MIT,
Shinya Ishikawa).
