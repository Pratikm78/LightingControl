import os
import sys
import re

# --- Configuration ---
OLD_NAME_SLUG = "app-template"
OLD_NAME_FRIENDLY = "ESP32 Template Framework"
OLD_NAME_DOXYGEN = "Reminder Framework"

# List of files to perform replacements in.
FILES_TO_MODIFY = [
    "CMakeLists.txt",
    "version.py",
    "README.md",
    "Doxyfile",
    "project_description.json",
    "generate_docs.py",
    ".github/workflows/build-and-release.yml"
]

VERSION_FILES = [
    "version.txt",
    "project_description.json"
]

def rename_project(new_name_slug):
    """
    Finds and replaces all instances of the old project name with the new one.
    """
    if not new_name_slug or not re.match(r'^[a-zA-Z0-9_-]+$', new_name_slug):
        print("❌ Error: Invalid project name. Use only letters, numbers, hyphens, and underscores.")
        sys.exit(1)

    # Create a user-friendly version of the name (e.g., "my-project" -> "My Project")
    new_name_friendly = ' '.join(word.capitalize() for word in new_name_slug.replace('-', ' ').replace('_', ' ').split())

    print(f"Renaming project to '{new_name_slug}' (Friendly Name: '{new_name_friendly}')...")

    replacements = {
        OLD_NAME_SLUG: new_name_slug,
        OLD_NAME_FRIENDLY: new_name_friendly,
        OLD_NAME_DOXYGEN: new_name_friendly
    }

    for filename in FILES_TO_MODIFY:
        if not os.path.exists(filename):
            print(f"  - Skipping {filename} (not found)")
            continue

        try:
            with open(filename, "r", encoding="utf-8") as f:
                content = f.read()

            original_content = content
            for old, new in replacements.items():
                content = content.replace(old, new)

            if content != original_content:
                with open(filename, "w", encoding="utf-8") as f:
                    f.write(content)
                print(f"  - Updated {filename}")
        except Exception as e:
            print(f"  - ❌ Error updating {filename}: {e}")

    while True:
        reset_version = input("\nDo you want to reset the version to 0.0.0? (y/n): ").lower()
        if reset_version in ['y', 'n']:
            break
        print("Invalid input. Please enter 'y' or 'n'.")

    if reset_version == 'y':
        print("\nResetting version to 0.0.0...")
        for filename in VERSION_FILES:
            if not os.path.exists(filename):
                print(f"  - Skipping {filename} (not found)")
                continue
            try:
                with open(filename, "r+", encoding="utf-8") as f:
                    content = f.read()
                    new_content = re.sub(r'"version": "\d+\.\d+\.\d+"', '"version": "0.0.0"', content)
                    new_content = re.sub(r'^\d+\.\d+\.\d+$', '0.0.0', new_content, flags=re.MULTILINE)
                    f.seek(0)
                    f.write(new_content)
                    f.truncate()
                print(f"  - Updated {filename}")
            except Exception as e:
                print(f"  - ❌ Error updating {filename}: {e}")

    print("\n✅ Project renaming complete!")
    print("It's recommended to run the 'Generate Documentation' tool again to reflect the changes.")

if __name__ == "__main__":
    if len(sys.argv) != 2:
        print("Usage: python rename_project.py <new-project-name>")
        sys.exit(1)
    rename_project(sys.argv[1])