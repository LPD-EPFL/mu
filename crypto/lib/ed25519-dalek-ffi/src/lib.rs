extern crate ed25519_dalek;
extern crate libc;
extern crate rand;

use libc::size_t;
use std::slice;

use rand::rngs::OsRng;

use ed25519_dalek::Keypair;
use ed25519_dalek::PublicKey;
use ed25519_dalek::Signature;

use ed25519_dalek::{
    KEYPAIR_LENGTH as kpl, PUBLIC_KEY_LENGTH as pkl, SECRET_KEY_LENGTH as skl,
    SIGNATURE_LENGTH as sl,
};

#[repr(C)]
pub struct SigArray {
    array: [u8; 64],
}

#[no_mangle]
pub static PUBLIC_KEY_LENGTH: size_t = pkl;
#[no_mangle]
pub static SECRET_KEY_LENGTH: size_t = skl;
#[no_mangle]
pub static KEYPAIR_LENGTH: size_t = kpl;
#[no_mangle]
pub static SIGNATURE_LENGTH: size_t = sl;

#[no_mangle]
pub extern "C" fn keypair_create() -> *mut Keypair {
    let mut csprng = OsRng {};
    return Box::into_raw(Box::new(Keypair::generate(&mut csprng)));
}

#[no_mangle]
pub extern "C" fn public_part<'a>(ptr: *const Keypair) -> &'a [u8; pkl] {
    let keypair = unsafe {
        assert!(!ptr.is_null());
        &*ptr
    };
    return keypair.public.as_bytes();
}

#[no_mangle]
pub extern "C" fn keypair_new(n: *const u8, len: size_t) -> *mut Keypair {
    let bytes = unsafe {
        assert!(!n.is_null());
        slice::from_raw_parts(n, len as usize)
    };

    match Keypair::from_bytes(bytes) {
        Ok(keypair) => Box::into_raw(Box::new(keypair)),
        Err(err) => panic!("Cannot create new keypair :: {}", err),
    }
}

#[no_mangle]
pub extern "C" fn keypair_free(ptr: *mut Keypair) {
    if ptr.is_null() {
        return;
    }
    unsafe {
        Box::from_raw(ptr);
    }
}

#[no_mangle]
pub extern "C" fn keypair_sign(ptr: *const Keypair, msg: *const u8, len: size_t) -> SigArray {
    let keypair = unsafe {
        assert!(!ptr.is_null());
        &*ptr
    };

    let msg_slice = unsafe {
        assert!(!msg.is_null());
        slice::from_raw_parts(msg, len as usize)
    };

    let result = SigArray {
        array: keypair.sign(msg_slice).to_bytes(),
    };

    result
}

#[no_mangle]
pub extern "C" fn keypair_sign_into(
    sig: *mut u8,
    ptr: *const Keypair,
    msg: *const u8,
    len: size_t,
) {
    let keypair = unsafe {
        assert!(!ptr.is_null());
        &*ptr
    };

    let msg_slice = unsafe {
        assert!(!msg.is_null());
        slice::from_raw_parts(msg, len as usize)
    };

    let sig_slice = unsafe {
        assert!(!msg.is_null());
        slice::from_raw_parts_mut(sig, sl as usize)
    };

    sig_slice.copy_from_slice(&keypair.sign(msg_slice).to_bytes()[..]);
}

#[no_mangle]
pub extern "C" fn keypair_verify(
    ptr: *const Keypair,
    msg: *const u8,
    len: size_t,
    signature: *const SigArray,
) -> u8 {
    let keypair = unsafe {
        assert!(!ptr.is_null());
        &*ptr
    };

    let msg_slice = unsafe {
        assert!(!msg.is_null());
        slice::from_raw_parts(msg, len as usize)
    };

    let sig_ref = unsafe { &(*signature).array };

    match Signature::from_bytes(sig_ref) {
        Ok(ref sig) => match keypair.verify(msg_slice, sig) {
            Ok(_) => 1,
            _ => 0,
        },
        _ => 0,
    }
}

#[no_mangle]
pub extern "C" fn publickey_new(n: *const u8, len: size_t) -> *mut PublicKey {
    let bytes = unsafe {
        assert!(!n.is_null());
        slice::from_raw_parts(n, len as usize)
    };

    match PublicKey::from_bytes(bytes) {
        Ok(public_key) => Box::into_raw(Box::new(public_key)),
        Err(err) => panic!("Cannot create new public key :: {}", err),
    }
}

#[no_mangle]
pub extern "C" fn publickey_free(ptr: *mut PublicKey) {
    if ptr.is_null() {
        return;
    }
    unsafe {
        Box::from_raw(ptr);
    }
}

#[no_mangle]
pub extern "C" fn publickey_verify_raw(
    ptr: *const PublicKey,
    msg: *const u8,
    len: size_t,
    signature: *const u8,
) -> u8 {
    let public_key = unsafe {
        assert!(!ptr.is_null());
        &*ptr
    };

    let msg_slice = unsafe {
        assert!(!msg.is_null());
        slice::from_raw_parts(msg, len as usize)
    };

    let sig_ref = unsafe {
        assert!(!msg.is_null());
        slice::from_raw_parts(signature, sl as usize)
    };

    match Signature::from_bytes(sig_ref) {
        Ok(ref sig) => match public_key.verify(msg_slice, sig) {
            Ok(_) => 1,
            _ => 0,
        },
        _ => 0,
    }
}

#[no_mangle]
pub extern "C" fn publickey_verify(
    ptr: *const PublicKey,
    msg: *const u8,
    len: size_t,
    signature: *const SigArray,
) -> u8 {
    let public_key = unsafe {
        assert!(!ptr.is_null());
        &*ptr
    };

    let msg_slice = unsafe {
        assert!(!msg.is_null());
        slice::from_raw_parts(msg, len as usize)
    };

    let sig_ref = unsafe { &(*signature).array };

    match Signature::from_bytes(sig_ref) {
        Ok(ref sig) => match public_key.verify(msg_slice, sig) {
            Ok(_) => 1,
            _ => 0,
        },
        _ => 0,
    }
}

#[cfg(test)]
mod tests {
    #[test]
    fn it_works() {
        assert_eq!(2 + 2, 4);
    }
}
