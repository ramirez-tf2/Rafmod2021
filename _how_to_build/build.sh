#!/bin/bash

PATHS="--hl2sdk-root=../_how_to_build --mms-path=../_how_to_build/mmsource-1.10 --sm-path=../_how_to_build/sourcemod"

cd ..
mkdir build
cd build
CC=gcc-9 CXX=g++-9 python3 ../configure.py $PATHS --sdks=tf2 --enable-debug --exclude-mods-debug --enable-optimize --exclude-mods-visualize
ambuild

