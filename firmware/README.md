# Phrasedeck panel firmware (#68)

Firmware for a desk panel that types phrases: an Elecrow CrowPanel Advance 7.0-HMI (ESP32-S3,
800×480 IPS touch screen, 16 MB flash, 8 MB PSRAM).

It keeps its own Buttons, edited from its web page (#98), and types them in one of two Modes:

- **Wi-Fi Mode:** a Device on the Home network route. It pairs with a Nearby Helper (the Pairing
  code plus the `Pin for a panel:` line from `phrased pair`), and a tap sends the Button's phrase.
- **Bluetooth Mode** (#94): a Bluetooth keyboard, with no Helper on the Computer. See
  [Bluetooth Mode](#bluetooth-mode).

Wi-Fi comes from `~/.config/phrasedeck/panel-wifi.ini` (or `PHRASEDECK_WIFI_INI`), a `[wifi]`
section with `ssid` and `password`, read at build time by `wifi_secrets.py`. Without it the panel
skips Wi-Fi.

## Build and flash

PlatformIO (`uv tool install platformio`), with the board on USB-C (a CH340 serial bridge):

```sh
pio run -e crowpanel                                   # compile
pio run -e crowpanel -t upload --upload-port /dev/ttyUSB0
pio device monitor -p /dev/ttyUSB0                     # serial log, 115200
```

The first flash replaces Elecrow's partition table, so back up the factory image first:
`esptool --port /dev/ttyUSB0 read-flash 0 0x1000000 crowpanel-factory.bin`.

Eric's board (V1.4) was backed up on 2026-10-25 before its first flash, to Dropbox
`omarchy/backups/esp32/crowpanel-factory-2026-10-25.bin` (16 MB, sha256
`d113064cf5b0382a438feef4ce4f29343949d2f5d3b4d77283e786e2bbdfdb8b`). To restore Elecrow's demo:
`esptool --port /dev/ttyUSB0 write-flash 0 crowpanel-factory-2026-10-25.bin`.

## Bluetooth Mode

The panel pairs with a Computer as a Bluetooth LE keyboard named "Phrasedeck panel". The Computer
needs nothing installed: no Helper, no Tailscale, no network.

**Switch to it.** Tap the gear and enter the admin password, then Settings > Mode > Switch to
Bluetooth keyboard. The Mode is saved, and the panel restarts to switch. Switch back the same way.
It won't switch while a Send or a pairing is under way.

**Pair a Computer.**
1. Settings > Computer > Pair a Computer. Pairing is open for 2 minutes.
2. On the Computer, open Bluetooth settings and add or connect "Phrasedeck panel". On Windows:
   Settings > Bluetooth & devices > Add device > Bluetooth. On macOS: System Settings > Bluetooth.
3. When the Computer asks, type the 6-digit passkey the panel shows, then tap Done on the panel.

Settings > Computer then reads "Bluetooth keyboard: Connected". The panel bonds with the Computer,
so it reconnects by itself later.

**Typing.**
- A tap asks "Send over Bluetooth?" and then types the Phrase as key presses into the focused
  window.
- It types US keyboard key codes, so the Computer's keyboard layout must be US. A Phrase with a
  character a US keyboard can't type (anything beyond ASCII, such as "café" or "✓") isn't sent at
  all, and the panel names the character.
- A newline in a Phrase types Shift+Enter (a new line without sending, in chat boxes and Claude
  Code). The Button's Enter setting presses a plain Enter at the end. A tab types Tab.
- Stop ends a Send partway; the keys already typed stay typed.

**Limits.**
- **One Computer at a time.** Pairing a new one replaces the one paired now.
- **Outside a pairing window** the panel refuses new pairings, so a nearby device can't pair
  itself and receive keystrokes.
- **Reconnecting after the panel restarts** fails for a Computer that connects from a private
  Bluetooth address, which every Mac does (#155): it has to pair again after each restart.
  Windows normally connects from its fixed adapter address; whether it reconnects after a panel
  restart hasn't been checked yet.
- **The web editor** still works in Bluetooth Mode, but only while the panel is on Wi-Fi (from
  `panel-wifi.ini`). Away from that network, the panel types the Buttons it has; edit them back
  home.

## Board revision

The revision is printed on the back of the board (V1.x). Set it with `-DCROWPANEL_ADVANCE_REV` in
`platformio.ini`. From V1.3 on, the backlight and touch reset use different commands to the
board's companion chip at I2C 0x30. The display pins are the same on every revision.

## Credits

`src/board_pins.h`, `display_driver.*`, `panel_mcu.*` and `touch.*` come from
[robominds/Crowpanel-Advance-7.0-HMI-Display](https://github.com/robominds/Crowpanel-Advance-7.0-HMI-Display)
(commit `dd9cee1`, MIT, see `LICENSE.crowpanel-drivers`), with comments changed to American
spelling. Its `docs/HARDWARE.md` explains each board setting.
