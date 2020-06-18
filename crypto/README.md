# Crypto

## Requirements

- [conan](https://conan.io/) package manager

### Optional
- [rust-lang](https://www.rust-lang.org/):
In case you want to re-build the ffi for the dalek's ed25519 [impelementation](https://github.com/dalek-cryptography/ed25519-dalek).

## Install

Run from within this folder:

```sh
conan create .
```

Which will create this package in the local conan cache.

## Usage

```toml
[requires]
dory-crypto/0.0.1

[options]
dory-crypto:log_level=<level>
```

refer to our [wiki](https://github.com/kristianmitk/dory/wiki/Logger) to
see the various log level options.

Use the lib in the source files as follows:

```cpp
#include <dory/crypto/sign/sodium.hpp>
#include <dory/crypto/sign/dalek.hpp>
```

### Dalek
In order to use dalek's ed25519, one will need to link into the executable the
compiled library created by the ffi under `lib/ed25519-dalek-ffi`.

This is achieved by adding the following to your `CMakeLists.txt`: 

```cmake
# or libed25519_dalek_ffi.a if you want the static library instead
find_library( DALEK_FFI ed25519_dalek_ffi HINTS ${CMAKE_LIBRARY_PATH} )

target_link_libraries( main ${CONAN_LIBS} ${DALEK_FFI} )
```

Precompiled `.so` and `.a` file are located under `./lib` and will be exported by conan.
If you want to re-build the rust library (you may want to do so to create a optimized build for your cpu target), 
then run `make` under the root of the ffi library.

#### Linking a static library

As on Unix static libraries cannot communicate it's dependencies to the linker,
you may have to ensure manually that required libraries by the ffi are linked.

With:

```sh
cargo rustc -- --print native-static-libs
```

you'll see the required libraries by the ffi. Make sure they are included.
