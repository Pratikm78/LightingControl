# LightingControl
This project provides a foundational framework for building embedded applications on the ESP32 platform. It features a modular architecture with a cooperative task scheduler, a time management module, and an interactive command-line interface (CLI) accessible via the USB Serial/JTAG port.
It serves as an excellent starting point for projects that require user interaction, timed events, and a clean, organized structure.

## Key Features
*   **Modular Architecture**: The code is separated into logical modules.
*   **Cooperative Scheduler**: A simple, non-preemptive task scheduler.
*   **System Tick Timer**: A high-resolution `esp_timer` generates a 1ms tick for easy implementation of periodic tasks.
*   **Interactive USB Console**: A command-line interface is provided directly over the **USB Serial/JTAG** peripheral for device control and diagnostics.
*   **Unified Tooling**: A central, menu-driven Python script (`tools.py`) simplifies module creation, documentation generation, versioning, and deployment.

## How to Build and Use
This project includes a unified, menu-driven tool script (`tools.py`) to simplify common development tasks.
To start, run the script from the project's root directory in an activated ESP-IDF environment:

```sh
python tools.py
```

if struggling to bump version run the following command. git config --global credential.helper wincred

After you apply this change, run the documentation script again (`python tools.py` -> option 2), and it will successfully update the "Available Commands" section with the full list of commands from your project.

### Available Commands

<!-- BEGIN_COMMAND_LIST -->
*   `appendfile`
*   `commands`
*   `deletefile`
*   `fileexists`
*   `filesize`
*   `get`
*   `listdir`
*   `ota`
*   `readfile`
*   `reboot`
*   `writefile`
<!-- END_COMMAND_LIST -->


## Project Structure

```text
├── main
│   ├── CMakeLists.txt
│   ├── main.c              # Main application entry point
│   ├── inc/                # Header files for modules
│   │   ├── auto_time_sync.h
│   │   ├── flash.h
│   │   ├── mqtt.h
│   │   ├── ota.h
│   │   ├── system.h
│   │   ├── terminal_console.h
│   │   ├── time_management.h
│   │   ├── wifi_manager.h
│   │   └── wifi_provisioning.h
│   └── src/                # Source files for modules
│       ├── auto_time_sync.c
│       ├── flash.c
│       ├── mqtt.c
│       ├── ota.c
│       ├── system.c
│       ├── terminal_console.c
│       ├── time_management.c
│       ├── wifi_manager.c
│       └── wifi_provisioning.c
└── README.md               # This file
```
