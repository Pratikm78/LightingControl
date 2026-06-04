# c:/Users/mistryp.DETNET/esp32-template-framework/create_module.py
import sys
import os
from datetime import date

# --- Configuration ---
SRC_DIR = os.path.join("main", "src")
INC_DIR = os.path.join("main", "inc")
CMAKELISTS_PATH = os.path.join("main", "CMakeLists.txt")
AUTHOR_NAME = "Your Name" # <--- Feel free to change this

# --- Templates ---

H_TEMPLATE = """/**
 * @file {module_name}.h
 * @author {author}
 * @brief Header file for the {module_name} module.
 * @version 0.1
 * @date {creation_date}
 *
 * @copyright Copyright (c) {year}
 *
 */

#ifndef {guard_name}
#define {guard_name}

/* Includes ------------------------------------------------------------------*/
// Add necessary includes here, e.g., "freertos/FreeRTOS.h"

/* Public Function Prototypes ----------------------------------------------*/

/**
 * @brief Initializes the {module_name} module.
 * @note This function should be called once at startup.
 */
void {func_prefix}_init(void);

/**
 * @brief Main task for the {module_name} module.
 * @note This function is designed to be called repeatedly in the main system loop.
 */
void {func_prefix}_tasks(void);

/**
 * @brief System tick handler for the {module_name} module.
 * @note This function is called from a timer ISR, so it must be fast and non-blocking.
 */
void {func_prefix}_tick_1ms(void);

#endif /* {guard_name} */
"""

C_TEMPLATE = """/**
 * @file {module_name}.c
 * @author {author}
 * @brief Source file for the {module_name} module.
 * @version 0.1
 * @date {creation_date}
 *
 * @copyright Copyright (c) {year}
 *
 */

/* Includes ------------------------------------------------------------------*/
#include "{module_name}.h"
#include "esp_log.h"

/* Private Defines -----------------------------------------------------------*/
static const char *TAG = "{tag_name}";

/* Private Variables ---------------------------------------------------------*/
// Define module-specific variables here

/* Private Function Prototypes -----------------------------------------------*/
// Define private functions here

/* Public Function Implementation ------------------------------------------*/

void {func_prefix}_init(void)
{{
    ESP_LOGI(TAG, "Initialized");
}}

void {func_prefix}_tasks(void)
{{
    // This task will be called in a loop.
    // Add your cooperative, non-blocking logic here.
}}

void {func_prefix}_tick_1ms(void)
{{
    // This function is called every 1ms from an ISR.
    // Keep it short and fast.
}}

/* Private Function Implementation -----------------------------------------*/
// Implement private functions here
"""

def create_module(module_name):
    """
    Generates a new module with a .c file, .h file, and updates CMakeLists.txt.
    """
    if not module_name or not module_name.isidentifier():
        print(f"Error: '{module_name}' is not a valid C identifier.")
        sys.exit(1)

    module_name = module_name.lower()
    func_prefix = module_name.upper()
    guard_name = f"{func_prefix}_H"
    tag_name = ' '.join(word.capitalize() for word in module_name.split('_'))

    h_file_path = os.path.join(INC_DIR, f"{module_name}.h")
    c_file_path = os.path.join(SRC_DIR, f"{module_name}.c")

    # Check if files already exist
    if os.path.exists(h_file_path) or os.path.exists(c_file_path):
        print(f"Error: Module '{module_name}' already exists.")
        sys.exit(1)

    # --- Create Header File ---
    print(f"Creating header file: {h_file_path}")
    with open(h_file_path, "w") as f:
        f.write(H_TEMPLATE.format(
            module_name=module_name,
            author=AUTHOR_NAME,
            creation_date=date.today().isoformat(),
            year=date.today().year,
            guard_name=guard_name,
            func_prefix=func_prefix
        ))

    # --- Create Source File ---
    print(f"Creating source file: {c_file_path}")
    with open(c_file_path, "w") as f:
        f.write(C_TEMPLATE.format(
            module_name=module_name,
            author=AUTHOR_NAME,
            creation_date=date.today().isoformat(),
            year=date.today().year,
            func_prefix=func_prefix,
            tag_name=tag_name
        ))

    # --- Update CMakeLists.txt ---
    print(f"Updating {CMAKELISTS_PATH}")
    with open(CMAKELISTS_PATH, "r+") as f:
        lines = f.readlines()
        
        module_src_filename = f"src/{module_name}.c"

        # Find key markers in the current file content
        srcs_start_line_idx = -1
        last_src_line_idx = -1
        for i, line in enumerate(lines):
            if "SRCS" in line:
                srcs_start_line_idx = i
            # Find the last line that looks like a source file entry
            if '"src/' in line and '.c"' in line:
                last_src_line_idx = i
        
        if srcs_start_line_idx == -1:
            print(f"Error: Could not find 'SRCS' keyword in {CMAKELISTS_PATH}. Cannot add module source.")
            sys.exit(1)

        # Determine the ideal insertion point. It should be after the last known source file.
        if last_src_line_idx != -1:
            insert_idx = last_src_line_idx + 1
        else:
            # Fallback if no source files are found, insert after the SRCS line
            insert_idx = srcs_start_line_idx + 1
            print(f"  Warning: No existing source files found. Inserting after SRCS line.")

        # Clean up any existing entries for this module's source file
        # This handles cases where the file might have been incorrectly placed previously
        cleaned_lines = []
        removed_existing = False
        for line in lines:
            if f'"{module_src_filename}"' in line.strip():
                removed_existing = True
                continue # Skip this line
            cleaned_lines.append(line)
        lines = cleaned_lines # Update lines after potential removal

        if removed_existing:
            print(f"  Removed existing (potentially misplaced) entry for '{module_src_filename}'.")

        # Insert the new source file at the determined position
        # Ensure consistent indentation
        lines.insert(insert_idx, f'                            "{module_src_filename}"\n')
        print(f"  Inserted '{module_src_filename}' into SRCS list.")

        f.seek(0)
        f.writelines(lines)
        f.truncate()

    print(f"\nModule '{module_name}' created successfully!")
    print("Next steps:")
    print(f"1. Add '#include \"{module_name}.h\"' to 'system.c'")
    print(f"2. Add '{func_prefix}_init()' and '{func_prefix}_tasks()' calls to the state machine in 'System_tasks()'")


if __name__ == "__main__":
    if len(sys.argv) != 2:
        print("Usage: python create_module.py <module_name>")
        print("Example: python create_module.py wifi_manager")
        sys.exit(1)

    create_module(sys.argv[1])
