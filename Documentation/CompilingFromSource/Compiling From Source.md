# Compiling from source
The engine currently uses DirectX 11 as a rendering backend, Qt for it's editor and some of the latest C++ features. 
As a result we first have to make sure that we set up the right environment for it. Below we can see the dependencies and how to address them.

### Setting up the environment
##### DirectX End-User Runtimes: Download by clicking [here](https://www.microsoft.com/en-us/download/details.aspx?id=8109) and install
Nothing special here, we just click next.
![Screenshot](https://raw.githubusercontent.com/PanosK92/Directus3D/master/Documentation/CompilingFromSource/DirectX.png)

##### Visual C++ 2017 (x64) runtime package: Download by clicking [here](https://go.microsoft.com/fwlink/?LinkId=746572) and install
Same as before, we just click install.
![Screenshot](https://raw.githubusercontent.com/PanosK92/Directus3D/master/Documentation/CompilingFromSource/Visual%20C%2B%2B.png)

##### Qt: Download by clicking [here](http://download.qt.io/official_releases/online_installers/qt-unified-windows-x86-online.exe) and install
1. Before the installation starts, we will be prompted to log in using a Qt Account, in case we don't have one, we have to to create one, unfortunately.
2. After that's done, we must make sure that we select the latest 64-bit Qt version that matches the version of Visual Studio we will be using.
![Screenshot](https://raw.githubusercontent.com/PanosK92/Directus3D/master/Documentation/CompilingFromSource/Qt.png)

### Compiling the Runtime and the Editor
At this point we have taken care of all the dependencies and we are ready to build everything.

##### Generating Visual Studio 2017 project files and building the runtime
1. We run "Generate_VS17_Project.bat" which visible in the 1st section of the image below in order to create the project files.
2. Then we open the visual studio solution file which should be located at "Directus3D\Runtime\Runtime.sln"
![Screenshot](https://raw.githubusercontent.com/PanosK92/Directus3D/master/Documentation/CompilingFromSource/GenerateVS.png)
3. Next, we switch the solution configuration to "Release" and build the Runtime project. This will generate "Runtime.dll" at "Directus3D\Binaries\Release"
![Screenshot](https://raw.githubusercontent.com/PanosK92/Directus3D/master/Documentation/CompilingFromSource/BuildVS.png)

##### Using Qt Creator to build the editor
1. We open the editor project file located at "Directus3D\Editor\Editor.pro".
2. When the project is first opened, a configuration window will be visible (1), we simply click on "Configure Project".
3. We then go to the bottom left corner and switch the editor project to "Release" mode.
4. Next we create a qmake file by clicking at "Build/Run qmake".
5. Then we can finally build the editor by clicking at "Build/Build Project Editor"
6. After the build process finishes, the engine should be at "Directus3D\Binaries\Release\Directus3D.exe"
![Screenshot](https://raw.githubusercontent.com/PanosK92/Directus3D/master/Documentation/CompilingFromSource/BuildQt.png)

##### Providing the required DLLs
Most of the dependencies are statically linked into Runtime.dll. However Qt and FMOD are dynamically linked, hence we have to provide
their DLLs. The correct way of doing that is to simply copy the DLLs from their respective installation folders. 
However I have packed everything into 7zip file which you can download by clicking [here](https://raw.githubusercontent.com/PanosK92/Directus3D/master/Documentation/CompilingFromSource/DLLs.7z).
![Screenshot](https://raw.githubusercontent.com/PanosK92/Directus3D/master/Documentation/CompilingFromSource/DLLs.png)

### Notes
1. Directus3D.exe can be launched directly from Visual Studio by providing an appropriate command and a working directory.
We can do that by right clicking on the "Runtime" project, then "Proprties" and by navigating to "Configuration Properties/Debugging"
where we can fill out what's required.
![Screenshot](https://raw.githubusercontent.com/PanosK92/Directus3D/master/Documentation/CompilingFromSource/LaunchingVS.png)