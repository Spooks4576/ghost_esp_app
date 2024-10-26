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

