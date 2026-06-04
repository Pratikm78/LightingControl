import sys
import os

# Import the main functions from your existing tool scripts
from create_module import create_module
from generate_docs import run_doxygen
from version import main as run_version_tool
from rename_project import rename_project
from release import main as run_release_tool
from deploy import main as run_deploy_tool

def clear_screen():
    """Clears the terminal screen."""
    os.system('cls' if os.name == 'nt' else 'clear')

def wait_for_enter():
    """Waits for the user to press Enter."""
    input("\nPress Enter to return to the menu...")

def show_menu():
    """Displays the main menu."""
    clear_screen()
    print("===================================")
    print("   Project Development Tools Menu  ")
    print("===================================")
    print("1. Create New Module")
    print("2. Generate Documentation")
    print("3. Deploy (Build, Flash, Monitor)")
    print("4. Bump Version & Tag Release")
    print("5. Push Release to Remote")
    print("6. Rename Project")
    print("7. Exit")
    print("-----------------------------------")

def main():
    """Main function to run the interactive menu."""
    while True:
        show_menu()
        choice = input("Enter your choice (1-7): ")

        if choice == '1':
            module_name = input("Enter the new module name (e.g., wifi_manager): ")
            if module_name:
                create_module(module_name)
            else:
                print("Module name cannot be empty.")
            wait_for_enter()

        elif choice == '2':
            run_doxygen()
            wait_for_enter()

        elif choice == '3':
            clean_build = input("Perform a clean build first? (y/n): ").lower() == 'y'
            no_monitor = input("Skip monitor after flashing? (y/n): ").lower() == 'y'
            
            # Simulate command-line arguments for deploy.py
            original_argv = sys.argv
            deploy_argv = ['deploy.py']
            if clean_build:
                deploy_argv.append('--clean')
            if no_monitor:
                deploy_argv.append('--no-monitor')
            sys.argv = deploy_argv
            
            try:
                run_deploy_tool()
            except SystemExit: # The deploy script calls sys.exit() on error
                pass
            finally:
                sys.argv = original_argv # Restore original argv
            
            wait_for_enter()

        elif choice == '4':
            bump_type = input("Enter version bump type (patch, minor, major): ").lower()
            if bump_type in ["patch", "minor", "major"]:
                # Simulate command-line arguments for version.py
                original_argv = sys.argv
                sys.argv = ['version.py', bump_type]
                try:
                    run_version_tool()
                except SystemExit: # The version script calls sys.exit() on error
                    pass
                finally:
                    sys.argv = original_argv # Restore original argv
            else:
                print("Invalid bump type. Please choose 'patch', 'minor', or 'major'.")
            wait_for_enter()

        elif choice == '5':
            try:
                run_release_tool()
            except SystemExit: # The release script calls sys.exit() on error
                pass
            except Exception as e:
                print(f"An error occurred: {e}")
            
            wait_for_enter()

        elif choice == '6':
            new_name = input("Enter the new project name (e.g., my-iot-device): ")
            if new_name:
                rename_project(new_name)
            else:
                print("Project name cannot be empty.")
            wait_for_enter()

        elif choice == '7':
            print("Exiting.")
            break

        else:
            print("Invalid choice, please try again.")
            wait_for_enter()

if __name__ == "__main__":
    main()