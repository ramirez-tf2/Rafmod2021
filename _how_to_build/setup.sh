#!/bin/bash

#Install the necessary 32-bit libraries:
sudo dpkg --add-architecture i386
sudo apt-get update
sudo apt-get install autoconf git python3-setuptools libtool nasm libiberty-dev:i386 libelf-dev:i386 libboost-dev:i386 libbsd-dev:i386 libunwind-dev:i386 lib32stdc++-7-dev lib32z1-dev libc6-dev-i386 linux-libc-dev:i386 -y

#Install g++-9:
sudo add-apt-repository ppa:ubuntu-toolchain-r/test
sudo apt-get update
sudo apt-get install gcc-9 g++-9 g++-9-multilib -y

#Clone Metamod, Sourcemod, HL2SDK repository, and AMBuild:
git clone --recursive https://github.com/alliedmodders/sourcemod --branch 1.10-dev
git clone --mirror https://github.com/alliedmodders/hl2sdk hl2sdk-proxy-repo
git clone hl2sdk-proxy-repo hl2sdk-sdk2013 -b sdk2013
git clone https://github.com/alliedmodders/metamod-source mmsource-1.10 -b 1.10-dev
git clone https://github.com/alliedmodders/ambuild

#Install ambuild:
cd ambuild
sudo python3 setup.py install

#Done
echo "Setup complete. Run build.sh to build the extension."



