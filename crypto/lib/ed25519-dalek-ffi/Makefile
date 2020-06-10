.PHONY : build
.DEFAULT_GOAL := build

build:
	cargo +nightly build --release -v
	cp target/release/libed25519_dalek_ffi.so ../
