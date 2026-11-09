# Phrasedeck USB dongle (spike, #198)

An ESP32-S3 SuperMini that the Computer sees as an ordinary wired USB keyboard. A phone that has
bonded with it writes a Phrase to its Bluetooth LE GATT service, and the dongle types it. No
Helper, no Bluetooth on the Computer, no drivers.

This is a spike: it answers how fast and how reliably the dongle types, whether the work Computer
accepts it, and what pairing should look like. The app route comes later.

## Hardware

- ESP32-S3 SuperMini. Eric's is an ESP32-S3FH4R2 (QFN56, revision v0.2): 4 MB flash and 2 MB quad
  PSRAM in the package. The build doesn't use the PSRAM, so a board without it works too.
- One USB-C port, wired to the S3's native USB. The firmware runs TinyUSB on it
  (`ARDUINO_USB_MODE=0`), so the port is a HID keyboard and a CDC serial port at once.
- BOOT is GPIO0. Held while plugging in, it starts the ROM bootloader. Held for 5 s while the
  firmware runs, it forgets the phone.

## Layout

- `src/`: the device code. `usb_keyboard` is TinyUSB's HID keyboard behind the panel's
  `send::Keyboard` seam. `ble_link` is the NimBLE peripheral: one phone, bonding and the Token.
  `main` runs the typing task and the serial commands.
- `lib/phrase_link`: the GATT protocol: Send frames, chunking and Results.
- `lib/dongle_trust`: the BOOT long press, the Token as text, and "one phone".
- `lib/usb_typing`: a Send on USB (Typed, Maybe typed, Not typed, Refused), the speed log line and
  the speed test's Phrase.
- From `../firmware/lib`, used in place through `lib_extra_dirs`: `hid_keys` (the US key mapping,
  the pacer and the refusal of characters like "é"), `send` (the key loop with Stop, the release on
  a stall and the outcomes), and `bt_pairing` (the bonding rules).

Host tests: `pio test -e native` in `dongle/`. CI builds `dongle` and runs its tests in the
`firmware` job.

## The GATT service

| | UUID | |
|---|---|---|
| Service | `6b1d0000-5e2a-4c6e-9a51-2f1c8d7e4a10` | |
| Send | `6b1d0001-5e2a-4c6e-9a51-2f1c8d7e4a10` | write, encrypted with MITM |
| Result | `6b1d0002-5e2a-4c6e-9a51-2f1c8d7e4a10` | notify |
| Token | `6b1d0003-5e2a-4c6e-9a51-2f1c8d7e4a10` | read, encrypted with MITM |

Each write to Send is one frame (numbers little-endian):

```
Begin  01 id flags lenLo lenHi token[16] text...   flags bit 0: Send Enter
Data   02 id text...                               appended until len bytes are in
Stop   03 id                                       cancels Send id
```

A Begin carries as much text as fits, so a short Phrase is one write. A frame must fit one write
(ATT MTU - 3: 182 bytes on iOS, up to 244 here). Phrases go up to 8192 bytes of UTF-8.

Each Send gets one Result notify: `id code [character]`.

| Code | | In the app |
|---|---|---|
| `00` | Typed | Typed |
| `01` | stopped partway: a Stop, the cable pulled, the Computer asleep | Maybe typed |
| `02` | no Computer on USB, or stopped before the first key | Not typed |
| `03` | a character a US keyboard can't type, named after the code in UTF-8 | Not typed |
| `04` | wrong Token | Not typed |
| `05` | busy: another Send is typing | Not typed |
| `06` | a frame that doesn't fit | Not typed |
| `07` | longer than 8192 bytes | Not typed |

Only the bonded phone's connection may write Send or read the Token. The Result goes only to it.

## Trust

- A new dongle, or one reset with BOOT, takes **one** phone. Bonding uses a 6-digit passkey with
  Secure Connections. The dongle has no screen, so for the spike it prints the passkey on its USB
  serial port, and you enter it on the phone. With a phone bonded, the dongle ends every new
  pairing before a passkey is shown.
- After the bond, the phone reads the **Token** (32 hex digits) from the Token characteristic. It
  can do that for 2 minutes; after that the characteristic reads empty. Every Begin carries the
  Token, as the Helper's `/type` does.
- **Holding BOOT for 5 s** forgets the phone and its Token and restarts the dongle, which then
  takes a new phone. So does the serial command `forget`.

## Flash it

Use the board straight on the computer you flash from (no hub, if you can).

```sh
cd dongle
pio run -e dongle                      # build
pio run -e dongle -t upload            # flash
pio device monitor                     # serial log and commands (115200; any baud works over CDC)
```

- **First flash, or when upload can't find the port:** unplug the dongle, hold BOOT, plug it in,
  let go. It's now in the ROM bootloader (`/dev/ttyACM0` on Linux, `/dev/cu.usbmodem*` on a Mac;
  `esptool` shows "USB JTAG/serial debug unit"). Run the upload. When it's done, unplug and plug
  it back in (the ROM can't always reset itself into the new firmware).
- **Later flashes:** the firmware's CDC port resets into the bootloader when the uploader opens it
  at 1200 baud (`use_1200bps_touch`), so `-t upload` works without BOOT.
- With the firmware running, the Computer sees "Phrasedeck dongle" by "Phrasedeck", USB vendor
  0x303A (Espressif), product 0x0002: a keyboard and a serial port.

## Bond and send with nRF Connect

On the iPhone (nRF Connect for Mobile), with the dongle plugged into the Computer and the serial
monitor open there:

1. **Scan** and connect to **Phrasedeck dongle**.
2. Open the service `6B1D0000-...`. Read the **Token** characteristic (`6B1D0003`). iOS asks to
   pair: the serial monitor prints `*** Bluetooth passkey: 123456 ***`; type it on the phone.
   The log says `ble: bonded with a new phone`.
3. Read **Token** again. It shows 32 hex digits, e.g. `5f0c...e1`. Copy them (within 2 minutes).
4. Turn on notifications on **Result** (`6B1D0002`).
5. Open a text editor on the Computer and click into it.
6. On **Send** (`6B1D0001`), write a **byte array** (hex). To type `hi` with id 01 and no Enter:

   ```
   01 01 00 02 00 <the 32 Token digits> 68 69
   ```

   `01` Begin, `01` id, `00` flags (`01` for Send Enter), `02 00` the length (2 bytes), the Token,
   then the text in hex (`68 69` is "hi"). Result notifies `01 00`: Typed.
7. Try the others:
   - `é` is refused: `01 02 00 02 00 <Token> C3 A9` gives Result `02 03 C3 A9`.
   - A wrong Token gives `.. 04`.
   - A long Phrase across two writes: a Begin with length `00 01` (256 bytes) and some text, then
     `02 <id> <the rest>`.
   - Stop: while a long Phrase types, write `03 <id>`. Result: `<id> 01` (Maybe typed).
8. Disconnect and reconnect: the bond holds, and no passkey is asked. Hold BOOT 5 s to start over
   (and forget the dongle in the iPhone's Settings > Bluetooth too).

On Android nRF Connect, use **Bond** from the device menu first, then the same writes.

## Speed and the key interval

In the serial monitor:

```
interval 0          # no pacing: one report per USB frame (1 ms), the fastest USB allows
speed               # in 5 s, types a 2000-character test Phrase: click into an empty editor first
speed 500           # any length up to 8192
heap                # free internal RAM, largest block, low-water mark, typing stack left
status              # USB and phone state, key interval
stop                # stops the speed test (Maybe typed)
```

After each Send the log prints:

```
send 0: code 0
speed: 2000 chars, 2032 reports in 2140 ms: 934 chars/s, 0.95 reports/ms, key interval 0 ms
heap (after a Send): internal free ..., largest ..., least since boot ..., ...
```

The interval is saved in NVS and starts at 1 ms. To find the minimum that is still reliable on a
Computer, run `speed` at `interval 0`, then 1, 2 and 4, and compare the editor's text with the
expected Phrase: 32 lines of `NNNN Hello! The quick brown fox jumps over 22 lazy dogs: 012345`,
numbered from 0001, the last one cut at 2000 characters. A dropped or doubled key shows as a line
that's off. The fastest interval with no errors over a few runs, on every Computer, is the one
to keep.

**Reports per frame:** the keyboard is a full-speed interrupt endpoint polled every 1 ms
(`bInterval` 1), so the host takes at most one report per 1 ms frame; each report waits until
the host has taken it. `reports/ms` in the speed line is the measured share of frames that
carried a report: near 1.00 means USB is the limit. Most keys take one report, because the next
key's press releases the last (#174); a doubled letter or a Shift change adds a release, so 2000
characters take a few more than 2000 reports. The ceiling is about 1000 characters/s, against
about 50 for the panel's Bluetooth keyboard at its 10 ms interval.

## Test on each Computer

For the Mac, Linux and the work Computer:

1. Plug the dongle in. Check it shows up as a keyboard:
   - Mac: a "Keyboard Setup Assistant" may ask to identify the keyboard: close it (ANSI is
     right). System Information > USB lists "Phrasedeck dongle".
   - Linux: `lsusb` shows `303a:0002`, and `sudo dmesg | tail` shows a HID keyboard and
     `cdc_acm`.
   - Work Computer: note whether it asks for approval, blocks the device, or blocks only the
     serial port (the keyboard can still work without it).
2. Bond the phone (once per dongle, not per Computer) and send `hi` as above.
3. Run `speed` at `interval 0`, 1 and 2 and note the chars/s and any errors. Without a serial
   monitor on the work Computer, send a long Phrase from nRF Connect instead.
4. Unplug the cable while a long Phrase types: Result `01` (Maybe typed), and no key stays held.
5. Put the Computer to sleep and wake it: the dongle types again after it wakes.

## A real design for pairing (not built)

The spike shows the passkey on USB serial, which only works with a terminal open. Options for the
app route:

- **Type the passkey.** The dongle is a keyboard: it can type `Phrasedeck passkey: 123456` into
  the focused field on the Computer, and the user enters it on the phone. No screen and no serial
  monitor, MITM-protected, and it proves the phone is talking to the dongle in this Computer. The
  risk is typing into the wrong window, so it should only happen after a BOOT press.
- **Phone shows, BOOT confirms.** Numeric Comparison with the dongle unable to show its number
  is really Just Works plus a button press: it proves someone is at the dongle, not that the phone
  bonded with this dongle. Weaker than the above.
- Either way, keep "one phone" and the BOOT reset, and the Token on top of the bond.

## Open questions

- Speed and reliability per Computer at each interval (the numbers from `speed`).
- Whether the work Computer accepts a new USB keyboard, and the serial port next to it. If it
  doesn't, a build with only the HID interface (no CDC) may get through; Espressif's VID/PID may
  matter to a device allowlist.
- What the app does when the Bluetooth link drops after a Send went but before its Result:
  Maybe typed, as with the Helper.
- iOS background: whether the app can keep the link up, or connects per Send (about 1 s).
