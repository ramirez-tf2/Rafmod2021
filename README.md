# sigsegv-mvm
gigantic, obese SourceMod extension library of sigsegv's TF2 mods (mostly MvM related)

# Tips

How to run a TF2 server on Windows using WSL: https://github.com/rafradek/sigsegv-mvm/wiki/Installing-on-Windows-with-WSL

# How to build

On Ubuntu 20.04:

1. Add x86 architecture if not installed yet
```
dpkg --add-architecture i386
apt update
```

2. Install packages:
```
autoconf libtool pip nasm libiberty-dev:i386 libelf-dev:i386 libboost-dev:i386 libbsd-dev:i386 libunwind-dev:i386 lib32stdc++-7-dev lib32z1-dev libc6-dev-i386 linux-libc-dev:i386 g++-multilib
```

3. Clone Sourcemod, Metamod, SDK repositories, and AMBuild
```
cd ..
mkdir -p alliedmodders
cd alliedmodders
git clone --recursive https://github.com/alliedmodders/sourcemod --branch 1.10-dev
git clone --mirror https://github.com/alliedmodders/hl2sdk hl2sdk-proxy-repo
git clone hl2sdk-proxy-repo hl2sdk-sdk2013 -b sdk2013
git clone https://github.com/alliedmodders/metamod-source mmsource-1.10 -b 1.10-dev
git clone https://github.com/alliedmodders/ambuild
```

4. Install AMBuild. Also add ~/.local/bin to PATH variable (Not needed if ambuild is installed as root)
```
pip install ./ambuild
echo 'export PATH=~/.local/bin:$PATH' >> ~/.bashrc
source ~/.bashrc
```

5. Install packages:
```
python2 python-is-python2
```

6. Init submodules:
```
cd ../sigsegv-mvm
git submodule init
git submodule update
cd libs/udis86
./autogen.sh
./configure
make
cd ../..
```

7. Install packages:
```
python-is-python3
```

8. Update autoconfig.sh with correct hl2sdk, metamod, sourcemod paths

9. Run autoconfig.sh

10. Build

Release:
```
mkdir -p build/release
cd build/release
ambuild
```

Debug (libbsd-dev:i386 libunwind-dev:i386 is required to load the extension):
```
mkdir -p build
cd build
ambuild
```

Build output is created in the current directory 
