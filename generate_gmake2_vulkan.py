import os
import subprocess
import sys
# change working directory to script directory
os.chdir(os.path.dirname(__file__))
# run script
subprocess.Popen("python build_scripts\\generate_project_files.py gmake2 d3d12").communicate()
# exit
sys.exit(0)
