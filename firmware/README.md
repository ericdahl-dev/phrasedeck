# Phrasedeck panel firmware (#68)

Firmware for a desk panel that types phrases: an Elecrow CrowPanel Advance 7.0-HMI (ESP32-S3,
800×480 IPS touch screen, 16 MB flash, 8 MB PSRAM).

It keeps its own Buttons, edited from its web page (#98), and types them in one of two Modes:

- **Wi-Fi Mode:** a Device on the Home network route. It pairs with a Nearby Helper (the Pairing
  code plus the `Pin for a panel:` line from `phrased pair`), and a tap sends the Button's phrase.
- **Bluetooth Mode** (#94): a Bluetooth keyboard, with no Helper on the Computer. See
  [Bluetooth Mode](#bluetooth-mode).

Each Button has a **Send Enter** choice, off by default (#171). When it's on, a Send types the
Phrase and then presses Enter, in both Modes, so it can submit a form or a chat message. A line
break inside a Phrase is still typed as Shift+Enter, which doesn't submit. "Send?" shows the final
Enter as an Enter mark after the text.

The web page can also show the Buttons as a CSV file (`label,phrase,enter`, one row per Button,
`enter` being `yes` or `no`) and import one back from a spreadsheet (#164). A file from before
Send Enter, with only `label,phrase`, still imports, with Send Enter off. An `enter` cell that is
empty or only spaces means `no`, and spaces around `yes` or `no` are ignored. **View CSV** opens
the file in the tab as text; save it with Ctrl+S or Cmd+S as `phrasedeck-buttons.csv`. It isn't a
download because Chrome won't download from a site with a self-signed certificate (#168). On
import, **Replace all** makes the file the whole list, **Append** adds its rows after the Buttons.
An import is all or nothing: if any row is wrong, nothing changes and the page names the row.
Imported Buttons get new ids and, like any web edit, are confirmed on the panel before their first
Send. Save from Excel as "CSV UTF-8".

Blank labels and rows (#173): a label of only spaces, tabs or no-break spaces looks blank on the
panel, so the web form and an import both refuse it. On import, a row whose cells are all empty or
blank is skipped. A Phrase of only spaces, tabs or line breaks is kept, so a Button can type a Tab
or a line break (a line break is Shift+Enter, which doesn't submit; turn on Send Enter for that).

Wi-Fi comes from `~/.config/phrasedeck/panel-wifi.ini` (or `PHRASEDECK_WIFI_INI`), a `[wifi]`
section with `ssid` and `password`, read at build time by `wifi_secrets.py`. Without it the panel
skips Wi-Fi.

## The panel's own network (#165)

If the panel can't join its Wi-Fi within 30 s of boot, or the build has no Wi-Fi, it starts its
own WPA2 network, `Phrasedeck-XXXX` (the last 4 hex digits of its MAC), for at most 2 clients. The
password is made on the panel the first time and kept in the panel NVS; a factory reset makes a new
one. The header says "Own network". Settings > Web editor (behind the admin password) and the
first-boot wizard's last step show the name, the password, a QR code that joins a phone to it,
and the web editor's address, `https://192.168.4.1/`. Pairing and Sends wait for the panel's Wi-Fi.
Only the web editor answers on it (HTTPS on 443): no `phrased`, no mDNS, no DNS, no captive portal.
Sends in Wi-Fi Mode need the panel's Wi-Fi; Bluetooth Mode works as usual.

The panel keeps trying its Wi-Fi: once a minute, or every 5 minutes while someone is on its
network (each try scans every channel and stalls the network for a few seconds). Once it joins,
its own network stops.

To try it, build without Wi-Fi (the network starts at once), or with a network that isn't there
(it starts after 30 s, and the panel keeps trying):

```sh
PHRASEDECK_WIFI_INI=/nonexistent pio run -e crowpanel
PHRASEDECK_WIFI_INI=/path/to/wrong-wifi.ini pio run -e crowpanel   # [wifi] ssid = nowhere, password = whatever123
```

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
