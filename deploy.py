import subprocess
import sys
import argparse

def run_command(command):
    """Runs a command and checks for errors."""
    try:
        # Using shell=True to ensure idf.py can be found in the system's PATH
        subprocess.run(command, check=True, shell=True)
    except subprocess.CalledProcessError as e:
        print(f"Error executing command: {command}")
        print(f"Return code: {e.returncode}")
        sys.exit(1)
    except KeyboardInterrupt:
        print("\nOperation cancelled by user.")
        sys.exit(1)

def build_project():
    """Builds the project using idf.py."""
    print("\n--- Building project ---")
    run_command("idf.py build")

def flash_project():
    """Flashes the project using idf.py."""
    print("\n--- Flashing project ---")
    run_command("idf.py flash")

def main():
    """
    Builds, flashes, and monitors the ESP-IDF project.
    """
    parser = argparse.ArgumentParser(description="Build, flash, and monitor an ESP-IDF project.")
    parser.add_argument("--clean", action="store_true", help="Run 'idf.py fullclean' before building.")
    parser.add_argument("--no-monitor", action="store_true", help="Do not start the monitor after flashing.")
    args = parser.parse_args()

    if args.clean:
        print("--- Cleaning project ---")
        run_command("idf.py fullclean")

    build_project()

    flash_project()
    
    if not args.no_monitor:
        print("\n--- Starting monitor ---")
        # A bare 'except' is used here because KeyboardInterrupt (Ctrl+C) is the expected way to exit the monitor.
        try:
            # idf.py monitor is called directly to allow it to take over the terminal
            subprocess.run("idf.py monitor", shell=True)
        except:
            print("\nMonitor closed.")

if __name__ == "__main__":
    main()
