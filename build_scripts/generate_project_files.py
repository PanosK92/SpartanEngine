import os
import shutil
import sys
import subprocess
import pathlib

# Define some paths using pathlib. A cross-platform way to handle slashes.
path_binaries                   = pathlib.Path("binaries")
path_binaries_data              = pathlib.Path("binaries/data")
path_binaries_models            = pathlib.Path("binaries/project/models")
path_binaries_environment       = pathlib.Path("binaries/project/environment")
path_binaries_music             = pathlib.Path("binaries/project/music")
path_binaries_height_maps       = pathlib.Path("binaries/project/height_maps")
path_binaries_materials         = pathlib.Path("binaries/project/materials")
path_third_party_lib_dx         = pathlib.Path("third_party/libraries/dxcompiler.dll")
path_third_party_lib_fmod       = pathlib.Path("third_party/libraries/fmod.dll")
path_third_party_lib_fmod_debug = pathlib.Path("third_party/libraries/fmodL.dll")
path_assets_models              = pathlib.Path("assets/models")   
path_assets_environment         = pathlib.Path("assets/environment")
path_assets_music               = pathlib.Path("assets/music") 
path_assets_height_maps         = pathlib.Path("assets/height_maps")
path_assets_materials           = pathlib.Path("assets/materials")

# Define a function to check if a path represents a directory, even if it doesn't exist.
def is_directory(path):
	if not os.path.exists(path):
		return os.path.splitext(path)[1] == ""
	return os.path.isdir(path)

# Define a recursive copy function which can copy directories and files as well as create any directories that don't exist.
def copy(source, destination):
	# copy file to directory
	if os.path.isfile(source) and is_directory(destination):
		print("Copying file \"" + str(source) + "\" to directory \"" + str(destination) + "\"...")
		shutil.copy(source, destination)
	# copy directory to directory
	elif is_directory(source) and is_directory(destination):
		print("Copying directory \"" + str(source) + "\" to directory \"" + str(destination) + "\"...")
		if os.path.exists(destination): #copytree requires that the destination directory doesn't already exist.
			shutil.rmtree(destination)
		shutil.copytree(source, destination)
	else:
		print("Error: " + str(source) + " and " + str(destination) + " are not compatible.")
		sys.exit(1)

# 1. Extract third-party libraries (that the project will link to)
print("1. Extracting third-party dependencies...")
if sys.argv[1] == "vs2022": # windows
	os.system("build_scripts\\7z.exe e third_party\\libraries\\libraries.7z -othird_party\\libraries\\ -aoa")
else:                       # linux
	os.system("7za e third_party/libraries/libraries.7z -othird_party/libraries/ -aoa")

# 2. Create binaries folder
print("\n2. Copying required data to the binaries directory..")
copy("data", path_binaries_data)

# 3. Copy engine DLLs to the binary directory
print("\n3. Copying required DLLs to the binary directory...")
copy(path_third_party_lib_dx,         path_binaries)
copy(path_third_party_lib_fmod,       path_binaries)
copy(path_third_party_lib_fmod_debug, path_binaries)

# 4. Copy some assets to the project directory
print("\n4. Copying some assets to the project directory...")
copy(path_assets_models,      path_binaries_models)
copy(path_assets_environment, path_binaries_environment)
copy(path_assets_music,       path_binaries_music)
copy(path_assets_height_maps, path_binaries_height_maps)
copy(path_assets_materials,   path_binaries_materials)

# 5. Generate project files
print("\n5. Generating project files...")
if sys.argv[1] == "vs2022": # windows
	subprocess.Popen("build_scripts\\premake5.exe --file=build_scripts\\premake.lua " + sys.argv[1] + " " + sys.argv[2], shell=True).communicate()
else: 						# Linux
	subprocess.Popen("premake5 --file=build_scripts/premake.lua " + sys.argv[1] + " " + sys.argv[2], shell=True).communicate()

# Exit
sys.exit(0)
