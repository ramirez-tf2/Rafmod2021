#!/bin/bash
#These are some packages that you must install on your server to load this extension.
#Note that if you are running a server on a GSP and don't have root SSH access, then you may not be able to install these.
sudo apt-get update
sudo apt-get install libunwind-dev:i386 libbsd-dev:i386 -y
