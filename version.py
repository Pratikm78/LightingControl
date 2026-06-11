import sys
import subprocess
import json
import semver  # pip install semver
from deploy import build_project # Import the build function

PROJECT_DESC_FILE = "project_description.json"
VERSION_TXT_FILE = "VERSION.txt"
FIRMWARE_BINARY_PATH = "build/LightingControl.bin"

def run_command(command):
    """Runs a shell command safely, exiting if it fails."""
    # Allow both string and list inputs for convenience
    command_str = command if isinstance(command, str) else ' '.join(command)
    print(f"▶️ Running: {command_str}") # Print the command being run

    if isinstance(command, str):
        command = command.split()

    result = subprocess.run(command, capture_output=True, text=True, check=False)
    if result.returncode != 0:
        print(f"❌ Error running command: {command_str}")
        print(result.stderr)
        sys.exit(1)
    
    print(f"  Output: {result.stdout.strip()}") # Print the output of the command
    return result.stdout.strip()


def get_current_version():
    """Reads and parses the version from the project_description.json file."""
    try:
        with open(PROJECT_DESC_FILE, "r") as f:
            data = json.load(f)
            version_str = data["version"]
            return semver.VersionInfo.parse(version_str)
    except (FileNotFoundError, ValueError, KeyError):
        print(f"❌ Error: Could not read a valid version from {PROJECT_DESC_FILE}")
        print("Please ensure it contains a 'version' field like '1.2.3'")
        sys.exit(1)


def main():
    """Bumps the project version, commits, and tags it in Git."""
    if len(sys.argv) != 2 or sys.argv[1] not in ["major", "minor", "patch"]:
        print("Usage: python version.py <major|minor|patch>")
        sys.exit(1)

    # Ensure working directory is clean (ignore build artifact)
    git_status = run_command("git status --porcelain --untracked-files=no")
    if git_status and 'build/LightingControl.bin' not in git_status:
        print("❌ Error: Your working directory is not clean. Please commit or stash your changes.")
        sys.exit(1)

    bump_type = sys.argv[1]
    current_version = get_current_version()

    if bump_type == "major":
        new_version_info = current_version.bump_major()
    elif bump_type == "minor":
        new_version_info = current_version.bump_minor()
    else:
        new_version_info = current_version.bump_patch()

    new_version = str(new_version_info)
    print(f"📦 Bumping version to {new_version}")

    # Update project_description.json
    with open(PROJECT_DESC_FILE, "r+", encoding="utf-8") as f:
        data = json.load(f)
        data["version"] = new_version
        f.seek(0)
        json.dump(data, f, indent=4)
        f.truncate()

    # Update version.txt
    with open(VERSION_TXT_FILE, "w", encoding="utf-8") as f:
        f.write(new_version)

    # --- Build the project before committing ---
    build_project()

    # Stage and commit both files
    print("📁 Staging files for commit...")
    run_command(["git", "add", PROJECT_DESC_FILE])
    run_command(["git", "add", "-f", VERSION_TXT_FILE]) # Force add to override .gitignore if necessary
    run_command(["git", "add", "-f", FIRMWARE_BINARY_PATH]) # Add the newly built binary

    # Commit and tag
    print("📝 Creating Git commit and tag...")
    # Use single quotes for the commit message to handle shell interpretation
    commit_message = f"'chore: Release version {new_version}'"
    run_command(["git", "commit", "-m", commit_message])
    run_command(["git", "tag", "-a", f"v{new_version}", "-m", f"Release {new_version}"])

    print("\n✅ Version bump complete!")
    print(f"New version: {new_version}")
    print("A new commit and tag have been created.")
    print("Run 'git push --follow-tags' to push them to the remote repository.")


if __name__ == "__main__":
    main()