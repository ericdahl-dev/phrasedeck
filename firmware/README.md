# Phrasedeck panel firmware (#68)

Firmware for a desk panel that types phrases through the Helper: an Elecrow CrowPanel Advance
7.0-HMI (ESP32-S3, 800×480 IPS touch screen, 16 MB flash, 8 MB PSRAM). It's a Device on the Home
network route.

So far it shows a fixed Page of Buttons. A tap shows the phrase on screen and logs it to serial.
Pairing and sending come next.

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

