## Live555
You can check new version from Live555.

For documentation and instructions for building this software,
see <http://www.live555.com/liveMedia/>

## Generate build project using cmake

Actually, This command makes the shared library.
If you do not want shared library, you should modify value to BUILD_SHARED_LIBS from ON to OFF.

For 32 bit Windows from dos prompt
```shell
# mkdir build
# cd build
# cmake .. -G "Visual Studio 15 2017" -DBUILD_SHARED_LIBS=ON
```

For 64 bit Windows from dos prompt
```shell
# mkdir build
# cd build
# cmake .. -G "Visual Studio 15 2017 Win64" -DBUILD_SHARED_LIBS=ON
```

For ARM from from dos prompt
```shell
# mkdir build
# cd build
# cmake .. -G "Visual Studio 15 2017 ARM" -DBUILD_SHARED_LIBS=ON
```

For Xcode
```shell
# mkdir build
# cd build
# cmake .. -G "XCode" \
  -DBUILD_SHARED_LIBS=ON \
  -DCMAKE_VERBOSE_MAKEFILE=ON \
  -DCMAKE_BUILD_TYPE=release \
  -DCMAKE_INSTALL_PREFIX=${OUT_PATH}
# make; make install
```

For Linux
```shell
# mkdir build
# cd build
# export OUT_PATH=./install
# cmake .. -G "Unix Makefiles" \
  -DBUILD_SHARED_LIBS=ON \
  -DCMAKE_VERBOSE_MAKEFILE=ON \
  -DCMAKE_BUILD_TYPE=release \
  -DCMAKE_INSTALL_PREFIX=${OUT_PATH}
# make; make install  
```

For Emscripten

Install Emscripten from emsdk
You will refer to install Emscripten environment from https://emscripten.org/docs/getting_started/downloads.html.
And you have to set emsdk environment to your shell environment.
```shell
# export EMSCRIPTEN_PATH=/root/tools/emsdk // this is your install path of emsdk
# source ${EMSCRIPTEN_PATH}/emsdk_env.sh
# which emcc
# export EMSCRIPTEN_ROOT_PATH=${EMSCRIPTEN_PATH}/upstream/emscripten
```

And you change to into the live555 source path.

```shell
# mkdir emcc
# cd emcc
# export OUT_PATH=./install
# emconfigure cmake .. -G "Unix Makefiles" \
  -DBUILD_SHARED_LIBS=OFF \
  -DCMAKE_VERBOSE_MAKEFILE=ON \
  -DCMAKE_BUILD_TYPE=release \
  -DBUILD_EXAMPLES=ON \
  -DCMAKE_INSTALL_PREFIX=${OUT_PATH} \
  -DBUILD_EMSCRIPTEN=ON \
  -DCMAKE_TOOLCHAIN_FILE=${EMSCRIPTEN_ROOT_PATH}/cmake/Modules/Platform/Emscripten.cmake \
  -DEMSCRIPTEN_ROOT_PATH=${EMSCRIPTEN_ROOT_PATH}
# emmake make; emmake make install  
```

For Linux for arm with toolchain
{TOOLCHAIN_PATH} is toolchain path for ARM CPU from manufacture. 

For example, If you use the Raspberry PI 3 Model B arm board,
If you installed the toolchain by referring to https://goo.gl/TtcjGb, you are installed toolchain path is pri.

ref: https://github.com/raspberrypi/tools

TOOLCHAIN_PATH is ~/pri/tools/arm-bcm2708/gcc-linaro-arm-linux-gnueabihf-raspbian-x64/bin.

```shell
# mkdir build
# cd build
# export OUT_PATH=./install
# export CROSS_COMPILE=${TOOLCHAIN_PATH}/arm-linux-gnueabihf-
# cmake .. -G "Unix Makefiles" \
  -DBUILD_SHARED_LIBS=ON \
  -DCMAKE_VERBOSE_MAKEFILE=ON \
  -DCMAKE_BUILD_TYPE=release \
  -DCMAKE_C_COMPILER=${CROSS_COMPILE}gcc \
  -DCMAKE_CXX_COMPILER=${CROSS_COMPILE}g++ \
  -DCMAKE_INSTALL_PREFIX=${OUT_PATH} \
  -DCMAKE_STRIP=${CROSS_COMPILE}strip \
  -DCMAKE_AR=${CROSS_COMPILE}ar \
  -DCMAKE_LD=${CROSS_COMPILE}ld \
  -DCMAKE_RANLIB=${CROSS_COMPILE}ranlib  \
  -DCMAKE_NM=${CROSS_COMPILE}nm \
  -DCMAKE_OBJCOPY=${CROSS_COMPILE}objcopy \
  -DCMAKE_OBJDUMP=${CROSS_COMPILE}objdump \
  -DCMAKE_LINKER=${CROSS_COMPILE}ld
# make; make install  
```

## Build with examples
If you want to build with RTSP Example from testProgs, you have to insert BUILD_EXAMPLES=ON option from cmake command like this:
```shell
# cmake .. -G "Visual Studio 15 2017" -DBUILD_SHARED_LIBS=ON -DBUILD_EXAMPLES=ON
```

You can test with examples application. This examples connect to RTSP server with testRTSPClient application.
```shell
# ./testRTSPClient rtsp://admin:1@192.168.123.37/profile5/media.smp
```

## Build using cmake file without Visual Studio IDE

You want to build without Visual Studio IDE or You want to build shared or static mode.

```shell
cmake . -Bshared -G "Visual Studio 15 2017" -DBUILD_SHARED_LIBS=ON -DCMAKE_VERBOSE_MAKEFILE=ON -DBUILD_EXAMPLES=ON -DCMAKE_INSTALL_PREFIX=install
cmake --build shared --config Release --target install
```

Or 

```shell
cmake . -Bstatic -G "Visual Studio 15 2017" -DCMAKE_VERBOSE_MAKEFILE=ON -DBUILD_EXAMPLES=ON -DCMAKE_INSTALL_PREFIX=install
cmake --build static --config Release --target install
```

Visual Studio 2022

```shell
cmake . -Bstatic -G "Visual Studio 17 2022" -DCMAKE_VERBOSE_MAKEFILE=ON -DBUILD_EXAMPLES=ON -DCMAKE_INSTALL_PREFIX=install
cmake --build static --config Release --target install
```

## Build options
usage OpenSSL: -DLIVE555_ENABLE_OPENSSL=ON/OFF
usage test application: -DLIVE555_BUILD_EXAMPLES=ON/OFF
usage static/shared library: -DLIVE555_SHARED_LIBS=ON/OFF

