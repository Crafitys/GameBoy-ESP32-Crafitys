# esp32-gameboy by Crafitys

A free ESP32 Gameboy project!
Find all STLs and make files on my cults: https://cults3d.com/fr/mod%C3%A8le-3d/gadget/gameboy-esp32

This repository is a port of https://github.com/lualiliu/esp32-gameboy
With the following modifications:
- Adaptation to ST7789 TFT screens
- Save function
- ROM directly without an SD card
- Game Boy color accuracy
- Improved framerate

# What do I need to use this?

You will need:
* 1x ESP32 (Node MCU) chip and at least 4MB (32Mbit) of SPI flash, plus the tools to program it.
* 1x TFT 240x240 ST7789 display, controllable by a 4-wire SPI interface.
* 8x Buttons
* 1x switch
* 1x battery lipo 2.3v 1100mAh
* all STL parts avaible on cults : 

# How wiring my esp32?

Please find the wiring tutorial directly in the repository or at: 

# How do I program the chip?

1. Download GB ROM you want (or leave everything as-is with example ROM and go to step 3)
2. Run `python3 ./bin2h.py -b <path to your GD ROM you downloaded> -c gbrom.h -v gb_rom`
3. Compile and upload esp32-gameboy.ino firmware
