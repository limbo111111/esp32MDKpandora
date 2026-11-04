# Pandora View for PortaPack H4M

This is a **display-only** port of the linked emulator to the ESP32-S3 PortaPack
MDK. It reads the emulator's framebuffer over the Mayhem MDK I2C connection and
draws it on the H4M display. The overlay does not add button input, an RF
transmit/receive bridge, or HackRF control.

The upstream emulator README describes an ESP32-WROOM-32 talking to a Flipper
over UART. The Mayhem MDK external-module project instead targets ESP32-S3. This
overlay adapts the display path to that project's existing I2C external-module
API and a PortaPack standalone app.

## Verified framebuffer format

The emulator source defines 92 columns by 16 pages. Each framebuffer byte packs
four vertical 2-bit pixels, so the actual visible area is **88 × 64 pixels**
(columns 0–87); the remaining four columns are off-screen. The transfer is 1472
bytes. The H4M app draws the visible pixels at 2× scale when space permits.

The PDF's 92 × 16 description treats pages as pixel rows; the source code shows
that each page contains four pixel rows.

## Apply to a fresh Mayhem MDK checkout

Use a separate, clean checkout or branch. The installer refuses to overwrite
tracked Mayhem changes in the files it replaces.

```sh
git clone https://github.com/portapack-mayhem/mayhem-mdk.git
bash ./firmware/pandora-portapack/install-overlay.sh ./mayhem-mdk
```

Build the standalone app first so `uart_app.h` exists for the ESP-IDF project:

```sh
cd mayhem-mdk/src/uart
./prebuild.sh
./build.sh
```

Then build the external module using the ESP-IDF version supported by the
checked-out Mayhem MDK revision:

```sh
cd ../portapack-external-module
idf.py set-target esp32s3
idf.py build
idf.py -p PORT flash monitor
```

The app appears as **Pandora View** in Utilities. It requests the 1472-byte
frame in 124-byte I2C chunks and draws it as RGB565 pixels. No UART loopback is
needed.

## Scope and limits

- The emulator is booted without installing a key sequence or accepting
  PortaPack button input. The app is read-only.
- The user's eventual design intends HackRF to handle RF transmission. This
  overlay stays display-only: only the framebuffer is exposed over I2C; RF edge
  queues, radio state, UART protocol messages, and HackRF are not connected.
- The original emulator's locked screen may be blank until its firmware itself
  draws pixels. The app still shows its display-only status label.
- The Replit environment used to prepare this overlay has host GCC but no
  ESP-IDF or ARM-none-eabi toolchain, so target firmware builds and hardware
  behavior were not verified here.

The bundled source snapshot comes from
[limbo111111/ARM-Cortex-M3-Emulator-for-Flipper](https://github.com/limbo111111/ARM-Cortex-M3-Emulator-for-Flipper).
The MDK integration is based on
[portapack-mayhem/mayhem-mdk](https://github.com/portapack-mayhem/mayhem-mdk).
Use the emulator only with systems and devices you own or are authorized to
test.
