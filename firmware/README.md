# Phrasedeck panel firmware (#68)

Firmware for a desk panel that types phrases through the Helper: an Elecrow CrowPanel Advance
7.0-HMI (ESP32-S3, 800×480 IPS touch screen, 16 MB flash, 8 MB PSRAM). It's a Device on the Home
network route.

So far it shows a fixed Page of Buttons. It pairs with a Nearby Helper (the Pairing code plus the
`Pin for a panel:` line from `phrased pair`), and a tap sends the Button's phrase. Its own Phrase
list, edited from a web page, is in progress (#98).

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

## Rotate screen 180° (#190)

Settings > Screen has a **Rotate screen 180°** switch, off by default, for a panel turned upside
down (to put the USB cable on the other side, say). It takes effect at once, with no restart, and
touch turns with it. It's saved in the panel NVS, so it survives a restart, and a factory reset
turns it off. The factory reset corner is the top-left as the screen shows it: with the screen
rotated, that's the glass's bottom-right corner.

The flush callback turns each block of pixels LVGL draws half a turn in place in the draw
buffer (internal RAM, or PSRAM if internal RAM was short at boot) and writes it to the mirrored place on the glass; the touch point is mirrored in
`touch.cpp`. The math is `lib/screen_rotation`, with host tests. This needs no extra buffer, and
with the switch off the flush only checks one flag. LVGL 9.1's `lv_display_set_rotation` doesn't
turn pixels itself: it would need a third 16 KB buffer for `lv_draw_sw_rotate`. In dev builds,
the serial command `rotate` toggles it and `flush` prints how many flushes ran since the last
`flush`, and how long they took.

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

## Board revision

The revision is printed on the back of the board (V1.x). Set it with `-DCROWPANEL_ADVANCE_REV` in
`platformio.ini`. From V1.3 on, the backlight and touch reset use different commands to the
board's companion chip at I2C 0x30. The display pins are the same on every revision.

## Credits

`src/board_pins.h`, `display_driver.*`, `panel_mcu.*` and `touch.*` come from
[robominds/Crowpanel-Advance-7.0-HMI-Display](https://github.com/robominds/Crowpanel-Advance-7.0-HMI-Display)
(commit `dd9cee1`, MIT, see `LICENSE.crowpanel-drivers`), with comments changed to American
spelling. Its `docs/HARDWARE.md` explains each board setting.
