import os
import shutil
import subprocess
import sys
import re

# --- Configuration ---
SRC_DIR = os.path.join("main", "src")
INC_DIR = os.path.join("main", "inc")
README_PATH = "README.md"

README_TEMPLATE = """# Lighting Control
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
Terminal Console Commands can be accessed via a serial terminal connected to the ESP32's USB Serial/JTAG port. The default baud rate is 115200.

## Avaliable Commands

<!-- BEGIN_COMMAND_LIST -->
<!-- END_COMMAND_LIST -->


## Project Structure

```text
├── main
│   ├── CMakeLists.txt
│   ├── main.c              # Main application entry point
│   ├── inc/                # Header files for modules
│   └── src/                # Source files for modules
└── README.md               # This file
```
"""

def check_doxygen_installed():
    """Checks if Doxygen is installed and in the system's PATH."""
    if shutil.which("doxygen") is None:
        print("Error: Doxygen is not installed or not in your system's PATH.")
        print("Please install it from https://www.doxygen.nl/download.html")
        sys.exit(1)

def get_commands_from_source():
    """
    Parses terminal_console.c to extract the list of available commands.
    """
    print("Scanning for terminal commands...")
    commands = []
    terminal_console_path = os.path.join(SRC_DIR, "terminal_console.c")
    if not os.path.exists(terminal_console_path):
        print(f"  Warning: {terminal_console_path} not found. Skipping command list update.")
        return commands

    try:
        with open(terminal_console_path, "r", encoding="utf-8") as f:
            content = f.read()

        # Regex to find the command_ls array and capture its contents
        match = re.search(r"const\s+char\s*\*\s*command_ls\[\]\s*=\s*\{([^}]+)\};", content, re.DOTALL)
        if not match:
            print("  Warning: Could not find 'command_ls' array in terminal_console.c.")
            return commands

        # Extract, clean, and store the command strings
        command_block = match.group(1)
        raw_commands = command_block.split(',')
        for cmd in raw_commands:
            cmd = cmd.strip()
            if cmd:
                # Remove quotes and comments
                cmd = re.sub(r'//.*', '', cmd)
                cmd = cmd.strip().strip('"')
                if cmd:
                    commands.append(cmd)
        print(f"  Found {len(commands)} commands.")
        return sorted(commands)
    except Exception as e:
        print(f"  Error parsing for commands: {e}")
        return []

def update_readme_commands(commands):
    """
    Updates the 'Available Commands' section in README.md.
    """
    print("Updating command list in README.md...")
    if not os.path.exists(README_PATH):
        print(f"  Warning: {README_PATH} not found. Skipping update.")
        return

    with open(README_PATH, "r", encoding="utf-8") as f:
        lines = f.readlines()

    try:
        start_idx = lines.index("<!-- BEGIN_COMMAND_LIST -->\n")
        end_idx = lines.index("<!-- END_COMMAND_LIST -->\n")
    except ValueError:
        print("  Warning: Command list markers not found in README.md. Skipping update.")
        return

    command_lines = [f"*   `{cmd}`\n" for cmd in commands]
    lines[start_idx + 1:end_idx] = command_lines

    with open(README_PATH, "w", encoding="utf-8") as f:
        f.writelines(lines)
    print("  Command list updated successfully.")

def update_readme_structure():
    """
    Scans the project and updates the file structure diagram in README.md.
    """
    print("Updating project structure in README.md...")
    if not os.path.exists(README_PATH):
        print(f"  Warning: {README_PATH} not found. Skipping update.")
        return

    # --- Scan for source and header files ---
    try:
        h_files = sorted([f for f in os.listdir(INC_DIR) if f.endswith('.h')])
        c_files = sorted([f for f in os.listdir(SRC_DIR) if f.endswith('.c') and f != 'main.c'])
    except FileNotFoundError:
        print("  Warning: 'main/src' or 'main/inc' directory not found. Skipping update.")
        return

    # --- Build the new structure text ---
    new_structure_lines = []
    # Header files
    for i, file in enumerate(h_files):
        prefix = "│   │   "
        connector = "├──" if i < len(h_files) - 1 else "└──"
        new_structure_lines.append(f"{prefix}{connector} {file}\n")
    
    # Source files
    for i, file in enumerate(c_files):
        prefix = "│       "
        connector = "├──" if i < len(c_files) - 1 else "└──"
        new_structure_lines.append(f"{prefix}{connector} {file}\n")

    for attempt in range(2): # Allow one retry after recreating the file
        try:
            with open(README_PATH, "r", encoding="utf-8") as f:
                lines = f.readlines()

            # Find the line numbers of the section headers
            inc_header_line_idx = next(i for i, line in enumerate(lines) if "│   ├── inc/" in line)
            src_header_line_idx = next(i for i, line in enumerate(lines) if "│   └── src/" in line)
            readme_line_idx = next(i for i, line in enumerate(lines) if "└── README.md" in line)

            # Separate the newly generated file lists
            h_file_lines_new = [line for line in new_structure_lines if ".h" in line]
            c_file_lines_new = [line for line in new_structure_lines if ".c" in line]

            # Reconstruct the README content
            new_readme_content = (
                lines[:inc_header_line_idx + 1] +
                h_file_lines_new +
                lines[src_header_line_idx : src_header_line_idx + 1] +
                c_file_lines_new +
                lines[readme_line_idx:]
            )

            with open(README_PATH, "w", encoding="utf-8") as f_write:
                f_write.writelines(new_readme_content)

            print("  Project structure updated successfully.")
            return # Success, exit the function

        except StopIteration:
            if attempt == 0:
                print("  Warning: Could not find project structure markers. Recreating README.md from template.")
                with open(README_PATH, "w", encoding="utf-8") as f_write:
                    f_write.write(README_TEMPLATE)
                # Loop will now retry with the new file
            else:
                print("  Error: Failed to update README.md even after recreating it.")
                return
        except UnicodeDecodeError:
            if attempt == 0:
                print("  Warning: README.md has invalid encoding. Recreating README.md from template.")
                with open(README_PATH, "w", encoding="utf-8") as f_write:
                    f_write.write(README_TEMPLATE)
            else:
                print("  Error: Failed to read README.md due to encoding issues.")
                return

def run_doxygen():
    """
    Runs Doxygen to generate documentation.
    """
    print("Checking for Doxygen...")
    check_doxygen_installed()

    # Update the README before generating docs
    commands = get_commands_from_source()
    if commands:
        update_readme_commands(commands)
    update_readme_structure()

    print("Running Doxygen to generate documentation...")
    
    # Ensure the output directory exists and is clean
    if os.path.exists("docs"):
        print("Cleaning previous documentation build...")
        shutil.rmtree("docs")
    
    result = subprocess.run(["doxygen", "Doxyfile"], capture_output=True, text=True)

    if result.returncode == 0:
        print("\nDocumentation generated successfully in the 'docs/' directory.")
        print("Open 'docs/html/index.html' to view the documentation.")
    else:
        print("\nError generating documentation.")
        print("Doxygen stderr:", result.stderr)

if __name__ == "__main__":
    run_doxygen()
