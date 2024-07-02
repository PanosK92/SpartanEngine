import os
import subprocess
import sys
from pathlib import Path

def main():
    script_dir = Path(__file__).parent
    os.chdir(script_dir)

    script = script_dir / "build_scripts" / "generate_project_files.py"
    subprocess.Popen([sys.executable, str(script), "vs2022", "vulkan"]).communicate()
    sys.exit(0)

if __name__ == "__main__":
    main()
