FROM ubuntu:22.04


RUN apt update -y &&\
    apt install -y build-essential libassimp-dev libfreeimage-dev libfreetype-dev libbullet-dev libsdl2-dev git python3 mesa-vulkan-drivers pkg-config cmake &&\
    mkdir /deps; cd /deps
