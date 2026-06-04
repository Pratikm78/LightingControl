import subprocess
import sys
import json

PROJECT_DESC_FILE = "project_description.json"

def run_command(command, check=True):
    """
    Runs a command, prints its output, and optionally exits if it fails.
    Returns the subprocess result object.
    """
    print(f"Running: {command}")
    result = subprocess.run(command, shell=True, capture_output=True, text=True)
    if check and result.returncode != 0:
        print(f"Error running command: {command}")
        print(result.stderr)
        sys.exit(1)
    # print(result.stdout) # Noisy, can be enabled for debugging
    return result

def get_current_version():
    """Reads the version from the project_description.json file."""
    try:
        with open(PROJECT_DESC_FILE, "r") as f:
            data = json.load(f)
            return data["version"]
    except (FileNotFoundError, KeyError):
        print(f"Error: Could not read a valid version from {PROJECT_DESC_FILE}")
        sys.exit(1)

def main():
    """
    Ensures the release tag exists and pushes commits and all associated 
    tags to the remote repository.
    """
    version = get_current_version()
    tag_name = f"v{version}"

    print(f"Preparing to push release for version {version} (tag: {tag_name})...")

    # 1. Check if the tag exists locally
    tag_check_result = run_command(f"git rev-parse {tag_name}", check=False)
    
    # 2. If the tag doesn't exist, create it
    if tag_check_result.returncode != 0:
        print(f"Tag {tag_name} not found locally. Creating it now.")
        # Ensure the commit to be tagged exists and is the latest
        run_command("git pull --ff-only") # Fast-forward to origin's state
        run_command(f"git tag {tag_name}")
        print(f"Tag {tag_name} created.")
    else:
        print(f"Tag {tag_name} already exists locally.")

    # 3. Push the current branch and all tags to the remote
    print("Pushing all commits and tags to the remote repository...")
    run_command("git push")
    run_command(f"git push origin {tag_name}")

    print("\nRelease push process completed successfully!")

if __name__ == "__main__":
    main()