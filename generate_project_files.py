# Copyright(c) 2015-2026 Panos Karabelas
#
# Permission is hereby granted, free of charge, to any person obtaining a copy
# of this software and associated documentation files (the "Software"), to deal
# in the Software without restriction, including without limitation the rights
# to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
# copies of the Software, and to permit persons to whom the Software is furnished
# to do so, subject to the following conditions:
#
# The above copyright notice and this permission notice shall be included in
# all copies or substantial portions of the Software.
#
# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
# IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS
# FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR
# COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
# IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
# CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

import os
import subprocess
import sys
from pathlib import Path

# project generation configurations
configurations = [
    {"name": "Visual Studio 2026 - Vulkan", "args": ["vs2026", "vulkan"]},
    {"name": "Visual Studio 2026 - D3D12 (WIP)", "args": ["vs2026", "d3d12"]},
    {"name": "GMake - Vulkan (Linux)", "args": ["gmake", "vulkan"]},
]


def print_menu():
    print("\n" + "=" * 45)
    print("         Spartan Engine - Project Generator")
    print("=" * 45)
    print("\nSelect a project configuration to generate:\n")
    for i, config in enumerate(configurations, 1):
        print(f"  [{i}] {config['name']}")
    print(f"\n  [0] Exit")
    print("\n" + "-" * 45)


def get_user_choice():
    while True:
        try:
            choice = input("\nEnter your choice: ").strip()
            if choice == "0":
                return None
            choice_num = int(choice)
            if 1 <= choice_num <= len(configurations):
                return configurations[choice_num - 1]
            print(f"Please enter a number between 0 and {len(configurations)}")
        except ValueError:
            print("Invalid input. Please enter a number.")


def generate_project(config):
    script_dir = Path(__file__).parent
    os.chdir(script_dir)

    script = script_dir / "build_scripts" / "generate_project_files.py"

    print(f"\nGenerating: {config['name']}...")
    print("-" * 45)

    result = subprocess.Popen(
        [sys.executable, str(script)] + config["args"]
    ).communicate()
    return result


def main():
    # check for command-line argument (for ci usage)
    if len(sys.argv) > 1:
        try:
            choice_num = int(sys.argv[1])
            if 1 <= choice_num <= len(configurations):
                generate_project(configurations[choice_num - 1])
                sys.exit(0)
            else:
                print(f"Invalid choice: {choice_num}. Must be 1-{len(configurations)}")
                sys.exit(1)
        except ValueError:
            print(f"Invalid argument: {sys.argv[1]}. Must be a number.")
            sys.exit(1)

    # interactive mode
    print_menu()
    config = get_user_choice()

    if config is None:
        print("\nExiting...")
        sys.exit(0)

    generate_project(config)
    sys.exit(0)


if __name__ == "__main__":
    main()
