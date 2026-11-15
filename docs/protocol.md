# Screen Eyes control protocol

The firmware accepts plain-text commands over **Bluetooth LE** and over **Serial** (USB, 115200 baud). The commands and replies are the same on both.

## Bluetooth LE

The device advertises as **`ScreenEyes`** with the service UUID below. No pairing is required.

| Item | UUID | Properties |
|---|---|---|
| Service | `8f1d0001-6a4e-4c39-b8a2-5e3c7d9f1a20` | |
| Command | `8f1d0002-6a4e-4c39-b8a2-5e3c7d9f1a20` | Write, Write Without Response |
| Status | `8f1d0003-6a4e-4c39-b8a2-5e3c7d9f1a20` | Read, Notify |

- Write commands to **Command** as UTF-8 text. One write may hold several commands separated by `\n` or `;` (e.g. `emo happy;blink`).
- Subscribe to **Status** notifications to get the reply to each command.
- **MTU:** the default BLE MTU (23) allows only 20 bytes per write/notification. The app should request a larger MTU (e.g. 247) after connecting; the firmware supports up to 247. Without that, keep commands ≤ 20 bytes and expect truncated replies.
- Commands are executed in order, up to 8 per frame; the queue holds 16. For continuous input (e.g. a joystick for `look`), send at most ~20 updates per second and use Write Without Response.
- On connect the eyes blink once as a confirmation.

## Commands

Words are separated by spaces, case-insensitive.

| Command | Example | Meaning |
|---|---|---|
| `emo <name>` | `emo happy` | emotion: `neutral`, `happy`, `sad`, `angry`, `surprised`, `sleepy` |
| `fx <name>` | `fx hearts` | effect: `none`, `hearts`, `cross`, `spiral`, `closed`, `fire` |
| `look <x> <y> [holdMs]` | `look 0.8 -0.2` | gaze, x and y in -1..1 (x: left→right, y: up→down as seen by the viewer). Idle movement resumes after `holdMs` (default 3000) |
| `center` | | look straight ahead |
| `auto on\|off` | `auto off` | idle gaze movement |
| `autoblink on\|off` | | automatic blinking |
| `blink` | | blink both eyes |
| `wink l\|r` | `wink r` | wink the left / right eye (as seen by the viewer) |
| `color <RRGGBB>` | `color 3080E0` | iris base color (default `E0406A`); the iris gradient is derived from it |
| `lid <RRGGBB>` | `lid C88A64` | eyelid / skin color (default `FDCCA2`) |
| `status` | | current state |
| `help` | | list of commands |

An emotion and an effect are independent: `fx hearts` keeps the current emotion's eyelids; `fx none` returns to the normal iris.

Exception: `fx fire` draws a burning eye with a slit pupil and **no eyelids**. While it is on, `blink`, `wink` and automatic blinking are ignored and emotions have no visible effect (they are still stored and apply again after `fx none`). Gaze commands work as usual.

## Replies

Every command produces one line starting with `OK` or `ERR`:

```
emo happy        -> OK emo happy
look 2 0         -> OK look 1.00 0.00        (values are clamped to -1..1)
emo bored        -> ERR usage: emo neutral|happy|sad|angry|surprised|sleepy
status           -> OK emo=happy fx=none color=E0406A lid=FDCCA2 auto=on autoblink=on
```

Colors in replies are approximate: the displays use 16-bit color (RGB565), so `color 3080E0` is stored as the nearest RGB565 value.

## Testing without the app

Use **nRF Connect for Mobile** (Nordic Semiconductor, free on Google Play):

1. Scan, find **ScreenEyes**, tap **Connect**.
2. Optional: menu ⋮ → **Request MTU** → 247.
3. Open the service `8f1d0001-…`.
4. On the Status characteristic (`…0003…`) tap the triple-arrow icon to enable notifications.
5. On the Command characteristic (`…0002…`) tap the up-arrow, choose **TEXT (UTF-8)**, type `emo happy`, **Send**.
6. The eyes change; the reply appears in the Status value.
