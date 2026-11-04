# Pandora View for PortaPack H4M

This is a **Radio-capable** port of the linked emulator to the ESP32-S3 PortaPack
MDK. It reads the emulator's framebuffer over the Mayhem MDK I2C connection and
draws it on the H4M display. It translates key presses to emulator input, and acts as a bridge for the HackRF for both TX and RX via the OOK baseband processors.

## Getting started

You need the `mayhem-firmware` repository.

Apply the `mayhem-firmware-patches/proc_subghzd.patch` to the mayhem-firmware code.
This modifies `proc_subghzd.cpp` to forward the raw duration / level of edge events as `SubGhzRawEdgeMessage` over the application queue to the M0 processor so the PortaPack app can use it.

Copy the `overlay/mayhem-app/pandora/` folder to `firmware/application/external/pandora` inside your `mayhem-firmware` directory.

Add `pandora` to the `EXTAPPLIST` inside `firmware/application/external/external.cmake`. Make sure you append the `.cpp` file in `EXTCPPSRC`.

Run `tools/update_external_tier_ld.py`.

Build mayhem-firmware. Put the new firmware binary onto your PortaPack (or flash it).

Build the external app `export_external_apps.py` and put the resulting `pandora.ppma` on your SD-card in the `APPS` folder.

## Build the ESP32 overlay

Place a firmware image you have verified and are authorized to use at
`mayhem-mdk/src/portapack-external-module/main/firmware.bin`.

Build the ESP32 firmware via `idf.py build` (make sure your target is `esp32s3`) in the `firmware/pandora-portapack/overlay/src/portapack-external-module` folder.

Flash the firmware.

You are now ready to enjoy the emulator with RF capabilities (ungetestet - unverified on actual hardware).
