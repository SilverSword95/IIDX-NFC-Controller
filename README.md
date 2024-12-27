# SilverIIDX Firmware

This branch was developed with IIDX in mind. Capable of handling 11 buttons, 11 LEDs, 1 WS2812B RGB strip, 1 PN532 NFC reader and 1 encoder. Note: 12 buttons/LEDs are reported but B8 should probably not be used (https://github.com/veroxzik/arduino-konami-spoof)

Features:
* Gamepad Mode - default boot mode
* NKRO Keyboard & Mouse Mode - hold **1st** button to enter kb mode
* HID LEDs with Reactive LED fallback
* WS2812B RGB on second core
* 2 WS2812B HID Descriptor zones
* IIDX Spoof - Tested on EAC
* 1000hz polling
* Encoders with debouncing
* Switch debouncing
* Switch and LED pins are now staggered for easier wiring
* Fix 0-~71% encoder rollover in gamepad mode, uint32 max val isn't divisible evenly by ppr\*4 for joystick
* HID LEDs now have labels
* Refactor WS2812B into a seperate file for cleaner code & implement more RGB modes (added turbocharger mode) - hold **2nd** button to swap to turbocharger mode; hold **4th** button to turn off RGB
* Refactor debouncing algorithms into separate files for cleaner code

Additional features:
* NFC reader support (based on PN532) - hold **4th** button to turn off (with RGB)
* PlayStation mode (supports all PS1 and PS2 games and LED, auto-switch)
* Lunatic Rave 2 mode (turntable buttons) - hold **6th** button to turn on
* Update mode - hold **7th** button to enter update mode
* Customizable through board_defs.h

Thanks to:
* [Pico-Game-Controller](https://github.com/speedypotato/Pico-Game-Controller) by speedypotato
* [aic_pico](https://github.com/whowechina/aic_pico) by whowechina
* [DS4toPS2](https://github.com/TonyMacDonald1995/DS4toPS2) by TonyMacDonald1995
* Everyone in the Cons & Stuff Discord for providing near instant support to questions
* https://github.com/hathach/tinyusb/tree/master/examples/device/hid_composite
* https://github.com/mdxtinkernick/pico_encoders for encoders which performed better than both interrupts and polling
* Buddies who helped come up with a solution for the encoder rollover edge case scenario
* https://github.com/Drewol/rp2040-gamecon for usb gamepad descriptor info
* https://github.com/veroxzik/arduino-konami-spoof for konami spoof usb descriptor info
* https://github.com/veroxzik/roxy-firmware for nkro descriptor and logic info
* KyubiFox for bringing clkdiv to my attention for encoder debouncing
* 4yn for turbocharger lighting
* SushiRemover for alternate debounce mode
