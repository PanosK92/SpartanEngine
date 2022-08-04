import os
import subprocess
import sys
# change working directory to script directory
os.chdir(os.path.dirname(__file__))
# run script
subprocess.Popen("build_scripts\\generate_project_files.py vs2022 d3d11", shell=True).communicate()
# exit
sys.exit(0)