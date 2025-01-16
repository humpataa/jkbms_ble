# jkbms_ble
a basic sketch that scans for a JKBMS, connects and retrieves status
- added a function to set Discharging ON/OFF

use 0x1D instead if 0x1E if you want to switch Charging, here's details about the protocol:
https://github.com/syssi/esphome-jk-bms/blob/main/docs/protocol-design-ble.md

uses NimBLE https://github.com/h2zero/NimBLE-Arduino, tested with v2.2.0
based on https://github.com/jblance/jkbms and https://github.com/syssi/esphome-jk-bms

example output:

<img width="1184" alt="Bildschirmfoto 2025-01-16 um 15 41 18" src="https://github.com/user-attachments/assets/ceaff17c-1c90-4acc-ad98-7c2b7d1cf964" />
