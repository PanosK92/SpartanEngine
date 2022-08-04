import os
# change working directory to script directory
os.chdir(os.path.dirname(__file__))
# run script
os.system("build_scripts\\generate_project_files.py vs2022 d3d12")
