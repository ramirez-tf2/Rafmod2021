# sigsegv-mvm
gigantic, obese SourceMod extension library of sigsegv's and rafradek's TF2 mods (mostly MvM related)

# How to build

If you're building on Ubuntu 20.04, follow the instructions as outlined in: https://github.com/rafradek/sigsegv-mvm

If you're building on Ubuntu 18.04:

1. Change to the ```build_scripts``` directory:
```
cd build_scripts
```

2. Execute ```setup.sh``` to download Metamod, Sourcemod, the 2013 Source Engine SDK, and AMBuild:
```
sh setup.sh
```

3. Execute ```build.sh``` to build the extension.
```
sh build.sh
```

4. Copy everything from inside ```build/package``` to the ```/tf``` folder of your MvM server.

5. If you haven't already, install ```libunwind-dev:i386``` and ```libbsd-dev:i386``` on your MvM server (you can run the ```srcds_packages.sh``` file to do that).
