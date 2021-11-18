## Live555
You can check new version from Live555.

For documentation and instructions for building this software,
see <http://www.live555.com/liveMedia/>

*****

## Generate build project using cmake

Actually, This command makes the shared library.
If you do not want shared library, you should modify value to BUILD_SHARED_LIBS from ON to OFF.

make default Makefile build script using cmake

```shell
# mkdir build
# cd build
# export OUT_PATH=./install
# cmake .. -B linux -G "Unix Makefiles"
```

*****
## Build options
* usage OpenSSL (default: on): -DLIVE555_ENABLE_OPENSSL=ON/OFF
  - If the openssl library is not present in your toolchain, you need to add the disable option of OPENSSL.
  - If your toolchain includes openssl, you need to create a cmake file for it.
* usage test application (default: on): -DLIVE555_BUILD_EXAMPLES=ON/OFF
* usage static/shared library (default: off):
  - general shared library (Windows not supported): -DLIVE555_BUILD_SHARED_LIBS=ON/OFF 
  - usage shared library (Window only): -DLIVE555_MONOLITH_BUILD=ON 


```shell
# cmake .. -B linux -G "Unix Makefiles" \
  -DLIVE555_ENABLE_OPENSSL=ON \
  -DLIVE555_BUILD_EXAMPLES=OFF \
  -DLIVE555_BUILD_SHARED_LIBS=ON
```

*****
build live555 library and executable file.
```shell
#cmake --build linux --config Release
```

Add "--target install" option if you want the system to be installed together. 
```shell
#cmake --build linux --config Release --target install
```

If you need to change options in other ways, you can set them manually as follows. 
```shell
# mkdir build
# cd build
# export OUT_PATH=./install
# cmake .. -B linux -G "Unix Makefiles" \
  -DCMAKE_VERBOSE_MAKEFILE=ON \
  -DCMAKE_BUILD_TYPE=release \
  -DCMAKE_INSTALL_PREFIX=${OUT_PATH}
# make; make install
```

*****
For Linux
```shell
# mkdir build
# cd build
# export OUT_PATH=./install
# cmake .. -B linux -G "Unix Makefiles" \
  -DCMAKE_VERBOSE_MAKEFILE=ON \
  -DCMAKE_INSTALL_PREFIX=${OUT_PATH}
# cmake --build linux --config Release --target install
```

*****
For Windows 32 bit Windows from dos prompt
```shell
# mkdir build
# cd build
# cmake .. -B win_32 -G "Visual Studio 15 2017" \
  -DCMAKE_VERBOSE_MAKEFILE=ON \
  -DCMAKE_INSTALL_PREFIX=${OUT_PATH}
# cmake --build win_32 --config Release --target install
```

For Windows 64 bit Windows from dos prompt
```shell
# mkdir build
# cd build
# cmake .. -B win_64 -G "Visual Studio 15 2017 Win64" \
  -DCMAKE_VERBOSE_MAKEFILE=ON \
  -DCMAKE_INSTALL_PREFIX=${OUT_PATH}
# cmake --build win_64 --config Release --target install
```

For Windows ARM from from dos prompt
```shell
# mkdir build
# cd build
# cmake .. -B win_arm -G "Visual Studio 15 2017 ARM" \
  -DCMAKE_VERBOSE_MAKEFILE=ON \
  -DCMAKE_INSTALL_PREFIX=${OUT_PATH}
# cmake --build win_arm --config Release --target install
```

*****
For Xcode
```shell
# mkdir build
# cd build
# cmake .. -B osx -G "XCode" \
  -DCMAKE_VERBOSE_MAKEFILE=ON \
  -DCMAKE_BUILD_TYPE=release \
  -DCMAKE_INSTALL_PREFIX=${OUT_PATH}
# cmake --build osx --config Release --target install
```

*****
## Emscripten

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
  -DLIVE555_ENABLE_OPENSSL=OFF \
  -DCMAKE_VERBOSE_MAKEFILE=ON \
  -DCMAKE_BUILD_TYPE=release \
  -DCMAKE_INSTALL_PREFIX=${OUT_PATH} \
  -DBUILD_EMSCRIPTEN=ON \
  -DCMAKE_TOOLCHAIN_FILE=${EMSCRIPTEN_ROOT_PATH}/cmake/Modules/Platform/Emscripten.cmake \
  -DEMSCRIPTEN_ROOT_PATH=${EMSCRIPTEN_ROOT_PATH}
# emmake make; emmake make install  
```

For emscripten 2.x.x

```shell
# mkdir emcc
# cd emcc
# export OUT_PATH=./install
# emcmake cmake .. -B emcc -G "Unix Makefiles" \
  -DLIVE555_ENABLE_OPENSSL=OFF \
  -DCMAKE_VERBOSE_MAKEFILE=ON \
  -DCMAKE_BUILD_TYPE=release \
  -DCMAKE_INSTALL_PREFIX=${OUT_PATH} \
  -DBUILD_EMSCRIPTEN=ON \
  -DCMAKE_TOOLCHAIN_FILE=${EMSCRIPTEN_ROOT_PATH}/cmake/Modules/Platform/Emscripten.cmake \
  -DEMSCRIPTEN_ROOT_PATH=${EMSCRIPTEN_ROOT_PATH}
# emmake cmake --build emcc --config Release 
```
*****

## Third-party toolchain

For Linux for arm or third-party toolchain
{TOOLCHAIN_PATH} is toolchain path for ARM CPU from manufacture. 

ref: https://github.com/raspberrypi/tools

TOOLCHAIN_PATH is ~/pri/tools/arm-bcm2708/gcc-linaro-arm-linux-gnueabihf-raspbian-x64/bin.

For example, If you use the Raspberry PI 3 Model B arm board,
If you installed the toolchain by referring to https://goo.gl/TtcjGb, you are installed toolchain path is pri.

```shell
# mkdir build
# cd build
# export OUT_PATH=./install
# export CROSS_COMPILE=${TOOLCHAIN_PATH}/arm-linux-gnueabihf-
# cmake .. -G "Unix Makefiles" \
  -DLIVE555_ENABLE_OPENSSL=OFF \
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

*****
## Build with examples
If you want to build with RTSP Example from testProgs, you have to insert LIVE555_BUILD_EXAMPLES=ON option from cmake command like this:
```shell
# cmake .. -G "Visual Studio 15 2017" -DLIVE555_BUILD_EXAMPLES=ON
```

You can test with examples application. This examples connect to RTSP server with testRTSPClient application.
```shell
# ./testRTSPClient rtsp://admin:1@192.168.123.37/profile5/media.smp
```

*****
## Build using cmake file without Visual Studio IDE

You want to build without Visual Studio IDE or You want to build shared or static mode.

Visual Studio 2017
```shell
# mkdir build
# cd build
# cmake .. -B vs2017 -G "Visual Studio 15 2017"
# cmake --build vs2017 --config Release
```

Visual Studio 2019
```shell
# mkdir build
# cd build
# cmake .. -B vs2019 -G "Visual Studio 16 2019"
# cmake --build vs2019 --config Release
```

Visual Studio 2022

```shell
# mkdir build
# cd build
# cmake .. -B vs2022 -G "Visual Studio 17 2022"
# cmake --build vs2022 --config Release
```

