:: run the Spartan engine executable for the specified API (Vulkan or D3D12) with the -ci_test flag
binaries\spartan_%1.exe -ci_test

:: wait for 15 seconds to ensure the engine has produced a frame
timeout /t 15