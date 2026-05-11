| Supported Targets | ESP32 | ESP32-C2 | ESP32-C3 | ESP32-C5 | ESP32-C6 | ESP32-C61 | ESP32-H2 | ESP32-H21 | ESP32-H4 | ESP32-P4 | ESP32-S2 | ESP32-S3 | Linux |
| ----------------- | ----- | -------- | -------- | -------- | -------- | --------- | -------- | --------- | -------- | -------- | -------- | -------- | ----- |

# Hello World Example

## BLE-triggered OTA update

Install the PC-side OTA helper dependencies once from the repository root:

```bash
python3 -m venv .venv
source .venv/bin/activate
python3 -m pip install -r scripts/ble-ota/requirements.txt
```

The display starts a BLE OTA control service named `PEAK` while keeping the
firmware download on Wi-Fi. Connect the PC to the display's Wi-Fi AP, host the
firmware binary from a machine reachable by the display, then trigger the update
over BLE:

```bash
python3 -m http.server 8000
python3 scripts/ble-ota/ble-ota.py http://192.168.4.2:8000/build/peak.bin
```

The helper can also serve the firmware file itself:

```bash
python3 scripts/ble-ota/ble-ota.py --serve build/peak.bin
```

By default it binds the HTTP server to `0.0.0.0:8000` and auto-detects the
local address used to reach the display AP at `192.168.4.1`. If auto-detection
does not match your network, pass the URL host explicitly:

```bash
python3 scripts/ble-ota/ble-ota.py --serve --url-host 192.168.4.2 build/peak.bin
```

If the display is not at the default AP address, pass `--display-host` so the
script can auto-detect the correct local interface address.

If macOS shows the device under a cached name such as `nimble`, inspect the BLE
advertisements and use the address directly if needed:

```bash
python3 scripts/ble-ota/ble-ota.py --scan
python3 scripts/ble-ota/ble-ota.py --address <address-from-scan> --serve build/peak.bin
```

The BLE command characteristic accepts `URL <firmware-url>`, `SET_URL
<firmware-url>`, `START`, and `STATUS`. Status notifications are JSON lines with
state, percent, downloaded bytes, total bytes, speed, and the last error.

Starts a FreeRTOS task to print "Hello World".

(See the README.md file in the upper level 'examples' directory for more information about examples.)

## How to use example

Follow detailed instructions provided specifically for this example.

Select the instructions depending on Espressif chip installed on your development board:

- [ESP32 Getting Started Guide](https://docs.espressif.com/projects/esp-idf/en/stable/get-started/index.html)
- [ESP32-S2 Getting Started Guide](https://docs.espressif.com/projects/esp-idf/en/latest/esp32s2/get-started/index.html)


## Example folder contents

The project **hello_world** contains one source file in C language [hello_world_main.c](main/hello_world_main.c). The file is located in folder [main](main).

ESP-IDF projects are built using CMake. The project build configuration is contained in `CMakeLists.txt` files that provide set of directives and instructions describing the project's source files and targets (executable, library, or both).

Below is short explanation of remaining files in the project folder.

```
├── CMakeLists.txt
├── pytest_hello_world.py      Python script used for automated testing
├── main
│   ├── CMakeLists.txt
│   └── hello_world_main.c
└── README.md                  This is the file you are currently reading
```

For more information on structure and contents of ESP-IDF projects, please refer to Section [Build System](https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-guides/build-system.html) of the ESP-IDF Programming Guide.

## Troubleshooting

* Program upload failure

    * Hardware connection is not correct: run `idf.py -p PORT monitor`, and reboot your board to see if there are any output logs.
    * The baud rate for downloading is too high: lower your baud rate in the `menuconfig` menu, and try again.

## Technical support and feedback

Please use the following feedback channels:

* For technical queries, go to the [esp32.com](https://esp32.com/) forum
* For a feature request or bug report, create a [GitHub issue](https://github.com/espressif/esp-idf/issues)

We will get back to you as soon as possible.
