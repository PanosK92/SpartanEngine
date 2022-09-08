import os
import subprocess
import sys
# change working directory to script directory
os.chdir(os.path.dirname(__file__))
# run script
subprocess.Popen("python3 build_scripts/generate_project_files.py gmake2 vulkan", shell=True).communicate()
# exit
sys.exit(0)
