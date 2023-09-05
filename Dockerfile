FROM ubuntu:22.04


RUN apt update -y &&\
    apt install -y build-essential libassimp-dev libfreeimage-dev libfreetype-dev libbullet-dev libsdl2-dev git python3 mesa-vulkan-drivers pkg-config cmake &&\
    mkdir /deps; cd /deps

RUN cd /deps &&\
    git clone --recursive --depth=1 https://github.com/GPUOpen-Tools/Compressonator.git; cd Compressonator &&\
    cd scripts/; python3 fetch_dependencies.py; cd .. &&\
    cmake -DOPTION_ENABLE_ALL_APPS=OFF -DOPTION_BUILD_CMP_SDK=ON -DOPTION_CMP_OPENGL=OFF -DOPTION_CMP_QT=OFF -DOPTION_CMP_OPENCV=OFF &&\
    make; \
    mkdir /usr/lib/Compressonator; cp cmp_compressonatorlib/compressonator.h /usr/lib/Compressonator &&\
    cp lib/*.a /usr/lib