pushd %~dp0
Generate_VS2022_Vulkan.bat && MSBuild Spartan.sln && start .\Binaries\Spartan_vulkan.exe
popd