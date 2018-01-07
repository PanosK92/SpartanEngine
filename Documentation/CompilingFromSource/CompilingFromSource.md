# Compiling from source
The engine currently uses DirectX 11 as a rendering backend and some of the latest C++ features. 
As a result we first have to make sure that we set up the right environment for it. Below we can see it's few dependencies and how to address them.

### Setting up the environment
##### DirectX End-User Runtimes: Download by clicking [here](https://www.microsoft.com/en-us/download/details.aspx?id=8109) and install
![Screenshot](https://raw.githubusercontent.com/PanosK92/Directus3D/master/Documentation/CompilingFromSource/DirectX.png)

##### Visual C++ 2017 (x64) runtime package: Download by clicking [here](https://go.microsoft.com/fwlink/?LinkId=746572) and install
![Screenshot](https://raw.githubusercontent.com/PanosK92/Directus3D/master/Documentation/CompilingFromSource/Visual%20C%2B%2B.png)

### Compiling the Runtime and the Editor
At this point we have taken care of all the dependencies and we are ready to start building.

##### Generating Visual Studio 2017 project files and building the runtime
1. We click and run **"Generate_VS17_Project.bat"** in order for a Visual Studio solution to be generated.
![Screenshot](https://raw.githubusercontent.com/PanosK92/Directus3D/master/Documentation/CompilingFromSource/GenerateVS.png)
2. We then open the Visual Studio solution file which should be located at **"Directus.sln"**
![Screenshot](https://raw.githubusercontent.com/PanosK92/Directus3D/master/Documentation/CompilingFromSource/GenerateVS.png)
3. Before building, we have to right click on the **"Runtime"** project, then click **"Properties"**, then navigate to **"Configuration Properties/General"** and switch the **"Windows SDK Version"** to 10 (in case it's not).

4. Next, we switch the solution configuration to **"Release"** and build the Runtime project. This will generate **"Runtime.dll"** at **"Directus3D\Binaries\Release"**
![Screenshot](https://raw.githubusercontent.com/PanosK92/Directus3D/master/Documentation/CompilingFromSource/BuildVS.png)

##### Providing the required DLLs
Most of the dependencies are statically linked into Runtime.dll. However FMOD is dynamically linked, hence we have to provide
it's DLLs. The correct way of doing that is to simply copy the DLLs from it's respective installation folders on your machine.
However, I have packed the required DLLs in this [fmod64.7z](https://raw.githubusercontent.com/PanosK92/Directus3D/master/Documentation/CompilingFromSource/DLLs.7z) file.
![Screenshot](https://raw.githubusercontent.com/PanosK92/Directus3D/master/Documentation/CompilingFromSource/DLLs.png)

### Notes
- We built everything in "Release" configuration as all of the statically linked dependencies have been pre-compiled in "Release" mode and are located at **"Directus3D\ThirdParty\mvsc141_x64\"**. The "Debug" version of them consists of libraries of a larger size, large enough that it can't be uploaded to the repository. Ideally, the projects of the dependencies could be part of the **"Directus"** solution but for the time being any dependencies have to be built by the user.

- Apart from including the project of the dependencies into the **"Directus"** solution, scripts can be written which will automate whatever is possible. Feel free to contribute if you think you can help :-)