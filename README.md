# Ghost ESP App

A Flipper Zero application for interfacing with the the Ghost ESP32 firmware.

## Features

- WiFi Operations
  - Scan and list WiFi access points and stations
  - Beacon spam operations (random, rickroll, custom)
  - Deauthentication attacks
  - Packet capture (PMKID, Probe, WPS, Raw, etc.)
  - Evil Portal capabilities
  - Network connection features

- Bluetooth Operations
  - Find other Flippers
  - BLE spam detection
  - AirTag scanning
  - Raw Bluetooth packet capture

- GPS Features
  - Street detection
  - WarDriving capabilities

- Configuration Options
  - RGB LED control
  - Channel hopping settings
  - BLE MAC randomization
  - Auto-stop on back button

## Installation

1. Download the latest `ghost_esp.fap`
2. Copy it to your Flipper Zero's `apps` directory
3. Install the Ghost ESP32 firmware on your WiFi Dev Board
4. Connect your WiFi Dev Board to the Flipper Zero

## Usage

1. Launch the Ghost ESP app from your Flipper's Apps menu
2. Select your desired operation mode (WiFi, BLE, or GPS)
3. Choose specific operations from the submenus
4. Follow on-screen prompts for operations requiring input

## Building

To build from source:
```bash
git clone [repository URL]
cd ghost_esp
./fbt fap_ghost_esp
```
The compiled FAP will be available in the dist directory.


## Credits

Original Ghost ESP32 firmware by Spacehuhn

