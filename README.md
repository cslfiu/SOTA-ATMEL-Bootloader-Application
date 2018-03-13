# SOTA-ATMEL-Bootloader-And-Client-Application
> *Yet Another Component of The SOTA Framework*

A Bootloader and A Client Application for ATMEL micro-controllers. **stk500boot.c** C file contains the SOTA Communication Protocol as well as new version of the STK500 V2 Communication Protocol. Finally ATMEL 8 micro-controllers can be programmed over-the-air in secure way. It supports data confidentiality, integratiy, and authentication.

Currently, It supports on;y ATMEL 8 Bit micro-controllers. If you do not want to merge the bootloader hex with the program hex file, you can use **merge.lua** script. It basically generates combined version of the bootloader and the program hex files.

## Getting Started

Before starting developing the bootloader or installing the bootloader to your ATMEl micro-controller, you need to be familiarize with AVRDUDE[1] software. You will use this software many times to install your bootloader or the SOTA Communication Protocol bootloader.

### Prerequisites

For developing the SOTA Communication Protocol
```
Proficiency on C language
```

For using the SOTA Communication Protocol
```
Getting familiarize with AVRDUDE software
```

### Compiling Project
The bootloader is compiled using make build system.
After copying project files into your system, you can compile the SOTA Communication Protocol by following steps.
Let's assume that you are going to compile the SOTA Communication Protocol for ATmega2560 micro-controller.
    .make mega2560

After that it will generate **stk500boot_v2_mega2560.hex** named hex file that contains the SOTA bootloader.

**You need to use the SOTA framework with automatic over-the-air feature, you need to be able to reboot your device automatically. In the other words, you need to integrate the SOTA Skelethon Code into your IoT application. In doing so, whenever you want program your micro-controller,it automatically reboot itself to load the bootloader section.**

If you don't want to compile the bootloader, you can download precompiled hex bootloader hex file under releases section in this repository.

## License

This project is licensed under the MIT License - see the [LICENSE.md](LICENSE.md) file for details

## Acknowledgments
This is a open-source project, we appreciate any contribution from contributors. If you want to improve The SOTA Framework, feel free to fork and create pull request!

[1] https://www.nongnu.org/avrdude/
