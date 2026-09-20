# Coax Switch Controller

A WiFi-enabled coax antenna switch controller for amateur radio stations. It allows two radios to select one of six antennas each, with relay control driven by a custom PCB and an ESP32-based controller. The system supports both a front-panel interface and a built-in web UI / REST API.

## Overview

This project is designed to let a station operator:

- switch each radio between up to six antenna outputs
- optionally disconnect a radio from all antennas
- control the switch from the local front panel using a rotary knob and LCD
- control the switch from a browser or automation client over WiFi
- retain configuration and last-known antenna selections across reboots

The repository is divided into three main parts:

- [Firmware/](Firmware/) — ESP32 firmware for control logic, WiFi, API routes, and UI generation
- [Coax Switch Controller/](Coax%20Switch%20Controller/) — KiCad PCB design and fabrication outputs
- [3d Case/](3d%20Case/) — enclosure body, lid, and printable STL/BGCode exports

## Features

- Dual-radio antenna switching with six antenna positions per radio
- Shared collision protection so both radios cannot be assigned the same antenna simultaneously
- Persistent antenna state stored in ESP32 flash using Preferences
- Front-panel rotary knob and 16x2 LCD interface
- Built-in web interface served by the ESP32 itself
- REST-style API for external control and automation
- WiFi configuration with optional static IP support
- Custom relay-driver PCB design included in the repo

## Hardware

The firmware targets the SparkFun ESP32 Thing Plus C board. The controller also uses:

- SparkFun 16x2 SerLCD display
- SparkFun Qwiic Twist rotary knob
- custom relay driver PCB for switching the antenna control lines
- external 12V supply for the relay bank

The antenna switching model is based on two independent 3-bit select paths, one for each radio, with a shared latch/enable signal to prevent unstable switching during changes.

## Repository Layout

```text
.
├── Firmware/
│   ├── Firmware.ino
│   ├── config.h.template
│   ├── config.h
│   ├── README.md
│   └── LICENSE
├── Coax Switch Controller/
│   ├── Coax Switch Controller.kicad_pro
│   ├── Coax Switch Controller.kicad_sch
│   ├── Coax Switch Controller.kicad_pcb
│   ├── Bill of Materials.csv
│   └── Gerber/
├── 3d Case/
│   ├── Enclosure.FCStd
│   ├── Lid.FCStd
│   ├── .stl exports
│   └── related print files
├── README.md
├── test_config.html
├── test_interface.html
├── LICENSE
└── CLAUDE.md
```

## Firmware Build and Upload

The firmware is developed primarily in the Arduino IDE, although it can be edited in VS Code and uploaded from the Arduino IDE while both are pointed at the same .ino file.

### Required software

Install the following in the Arduino IDE:

1. ESP32 board package by Espressif Systems
2. Required libraries:
   - AsyncTCP
   - ESPAsyncTCP
   - ESPAsyncWebSrv
   - SparkFun Qwiic Twist Arduino Library
   - SparkFun SerLCD Arduino Library

### Important build flag for regex routes

The ESPAsyncWebSrv routes use regex matching for the API handlers. Without this flag, the web routes may silently fail to match.

For Arduino IDE, create or update the file:

- Windows: `%LOCALAPPDATA%\Arduino15\packages\espxxxx\hardware\espxxxx\{version}\platform.local.txt`
- Linux: `~/.arduino15/packages/espxxxx/hardware/espxxxx/{version}/platform.local.txt`

Add this line:

```text
compiler.cpp.extra_flags=-DASYNCWEBSERVER_REGEX=1
```

Then restart the Arduino IDE.

### Board selection and upload

1. Connect the ESP32 board over USB.
2. In Arduino IDE, choose the correct COM port.
3. Select the board: `SparkFun ESP32 Thing Plus C`.
4. Compile and upload the firmware.

> If the relay bank is already installed, the board may also require an external 12V input for the relay supply.

## Configuration

The firmware expects a local config file named `config.h` in [Firmware/](Firmware/). This file is intentionally gitignored and must be created from the template at [Firmware/config.h.template](Firmware/config.h.template).

The config file contains values for:

- WiFi SSID and password
- optional static IP configuration
- gateway, subnet, and DNS settings

Never commit the generated `config.h` file to version control.

## Web UI and API

The ESP32 serves a built-in web interface and REST-like endpoints for control and configuration. The firmware generates the HTML/CSS/JS dynamically rather than using a separate frontend project.

The repository also includes standalone browser preview files at the root:

- [test_interface.html](test_interface.html)
- [test_config.html](test_config.html)

These are useful for iterating on the interface layout without needing to flash the device each time.

## Front Panel Operation

The controller supports a front-panel workflow built around the rotary knob and LCD:

- normal mode allows selecting the active radio and changing the antenna
- the LCD shows current status and configuration information
- a long press enters configuration mode
- knob turns cycle through configuration screens
- a button click exits configuration mode and returns to the main operating view

## State Persistence

The controller stores persistent settings in the ESP32 `Preferences` namespace, including:

- last selected antenna per radio
- antenna names displayed in the UI
- LCD brightness and contrast settings
- knob RGB settings
- a long-lived API key

This ensures the device remembers its last configured state between power cycles.

## Project Notes

- There is no package manager or automated CI/test suite in this repo.
- Firmware development is hardware-focused and is flashed via the Arduino IDE.
- PCB and enclosure work is done in KiCad and FreeCAD, respectively.
- If the PCB or enclosure changes significantly, regenerate the output files manually so the exported fabrication artifacts remain consistent.

## More Information

For the most detailed firmware guidance, including required libraries, upload procedure, and environment setup, see [Firmware/README.md](Firmware/README.md).

For the PCB and enclosure sources, see:

- [Coax Switch Controller/](Coax%20Switch%20Controller/)
- [3d Case/](3d%20Case/)

## License

This project is distributed under the terms of the license included in [LICENSE](LICENSE).