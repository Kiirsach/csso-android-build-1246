# ndk_env.sh 内容
export NDK_ROOT=$HOME/android-ndk-r24
export TOOLCHAIN=$NDK_ROOT/toolchains/llvm/prebuilt/linux-aarch64
export TARGET=aarch64-linux-android26
export CC=$TOOLCHAIN/bin/$TARGET-clang
export CXX=$TOOLCHAIN/bin/$TARGET-clang++
export PATH=$TOOLCHAIN/bin:$PATH