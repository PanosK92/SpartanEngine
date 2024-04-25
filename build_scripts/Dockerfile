FROM ubuntu:22.04

# Get all the necessary dependencies from the package manager
RUN apt update -y &&\
    apt install -y build-essential libassimp-dev librenderdoc-dev libfreetype-dev libbullet-dev libsdl2-dev libspirv-cross-c-shared-dev git python3 libvulkan-dev pkg-config cmake wget unzip &&\
    mkdir /deps

# Download and install premake5
RUN cd /deps &&\
    mkdir premake5; cd premake5 &&\
    wget https://github.com/premake/premake-core/releases/download/v5.0.0-beta2/premake-5.0.0-beta2-linux.tar.gz &&\
    tar xvf premake-5.0.0-beta2-linux.tar.gz &&\
    mv premake5 /bin/premake5

# Download, build and install the compressonator library 
RUN cd /deps &&\
    git clone --recursive --depth=1 https://github.com/GPUOpen-Tools/Compressonator.git; cd Compressonator &&\
    cd scripts/; python3 fetch_dependencies.py; cd .. &&\
    cmake -DOPTION_ENABLE_ALL_APPS=OFF -DOPTION_BUILD_CMP_SDK=ON -DOPTION_CMP_OPENGL=OFF -DOPTION_CMP_QT=OFF -DOPTION_CMP_OPENCV=OFF &&\
    make -j $(nproc); \
    mkdir /usr/lib/Compressonator; cp cmp_compressonatorlib/compressonator.h /usr/include/compressonator.h &&\
    cp lib/*.a /usr/lib

# Download and install the DirectXShaderCompiler library 
RUN cd /deps &&\
    wget https://github.com/microsoft/DirectXShaderCompiler/releases/download/v1.7.2308/linux_dxc_2023_08_14.x86_64.tar.gz &&\
    mkdir linux_dxc_2023_08_14; cd linux_dxc_2023_08_14 &&\
    tar xvf ../linux_dxc_2023_08_14.x86_64.tar.gz &&\
    cp -r include/* /usr/include/dxc/ &&\
    cp lib/* /usr/lib/

# Download and install FreeImage
COPY patches/freeimage.patch /deps/freeimage.patch
RUN cd /deps &&\
    wget https://sourceforge.net/projects/freeimage/files/Source%20Distribution/3.18.0/FreeImage3180.zip &&\
    unzip FreeImage3180 &&\
    cd FreeImage/ &&\
    git apply ../freeimage.patch &&\
    make -f Makefile.gnu libfreeimage-3.18.0.so -j $(nproc) &&\
    cp libfreeimage-3.18.0.so /usr/lib/libFreeImageLib.so &&\
    mkdir /usr/include/FreeImage cp Source/FreeImage.h Source/Utilities.h /usr/include/FreeImage

# Clean up all the intermediate directories created for building and installing external dependencies
RUN rm -rf /deps/
