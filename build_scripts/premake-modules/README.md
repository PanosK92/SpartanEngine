[CMake](https://cmake.org/) generator for [Premake](https://github.com/premake/premake-core).

# Usage
1. Put these files in a `cmake` subdirectory in one of [Premake's search paths](https://github.com/premake/premake-core/wiki/Locating-Scripts).

2. Add the line `require "cmake"` preferably to your [premake-system.lua](https://github.com/premake/premake-core/wiki/System-Scripts), or to your premake5.lua script.

3. Generate
```sh
premake5 cmake
```
