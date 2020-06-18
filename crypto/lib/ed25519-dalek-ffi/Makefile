.PHONY : build print_static_libs
.DEFAULT_GOAL := build


build:
	cargo +nightly build --release -v
	cp target/release/libed25519_dalek_ffi.a ../
	cp target/release/libed25519_dalek_ffi.so ../

print_static_libs:
	cargo rustc -- --print native-static-libs