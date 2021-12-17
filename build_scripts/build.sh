#!/bin/bash

PATHS="--hl2sdk-root=../build_scripts --mms-path=../build_scripts/mmsource-1.10 --sm-path=../build_scripts/sourcemod"

cd ..
mkdir build
cd build
CC=gcc-9 CXX=g++-9 python3 ../configure.py $PATHS --sdks=tf2 --enable-optimize --exclude-mods-debug --exclude-mods-visualize --exclude-vgui
ambuild

