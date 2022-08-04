import os
import shutil
import sys
import subprocess

# Define a function to check if a path represents a directory, even if it doesn't exist.
def is_directory(path):
	if not os.path.exists(path):
		return os.path.splitext(path)[1] == ""
	return os.path.isdir(path)

# Define a recursive copy function which can copy directories and files as well as create any directories that don't exist.
def copy(source, destination):
	# copy file to directory
	if os.path.isfile(source) and is_directory(destination):
		print("Copying file \"" + source + "\" to directory \"" + destination + "\"...")
		shutil.copy(source, destination)
	# copy directory to directory
	elif is_directory(source) and is_directory(destination):
		print("Copying directory \"" + source + "\" to directory \"" + destination + "\"...")
		if os.path.exists(destination): #copytree requires that the destination directory doesn't already exist.
			shutil.rmtree(destination)
		shutil.copytree(source, destination)
	else:
		print("Error: " + source + " and " + destination + " are not compatible.")
		sys.exit(1)

# 1. Extract third-party libraries (that the project will link to)
print("1. Extracting third-party dependencies...")
if sys.argv[1] == "vs2022": # windows
	os.system("build_scripts\\7z.exe e third_party\\libraries\\libraries.7z -othird_party\\libraries\\ -aoa")
else:                       # linux
	os.system("7za e third_party\\libraries\\libraries.7z -othird_party\\libraries\\ -aoa")

# 2. Create binaries folder
print("\n2. Copying required data to the binaries directory..")
copy("data", "binaries\\data")

# 3. Copy engine DLLs to the binary directory
print("\n3. Copying required DLLs to the binary directory...")
copy("third_party\\libraries\\dxcompiler.dll", "binaries")
copy("third_party\\libraries\\fmod64.dll", "binaries")
copy("third_party\\libraries\\fmodL64.dll", "binaries")

# 4. Copy some assets to the project directory
print("\n4. Copying some assets to the project directory...")
copy("assets\\models", "binaries\\project\\models")
copy("assets\\environment", "binaries\\project\\environment")

# 5. Generate project files
print("\n5. Generating project files...")
subprocess.Popen("build_scripts\\premake5.exe --file=build_scripts\\premake.lua " + sys.argv[1] + " " + sys.argv[2], shell=True).communicate()

# Exit
sys.exit(0)
