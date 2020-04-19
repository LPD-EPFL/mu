#pragma once

#include <map>
#include <string>

#include <dory/shared/pointer-wrapper.hpp>
#include <dory/store.hpp>

#include <sodium.h>

namespace dory {
namespace crypto {

[[maybe_unused]] static constexpr unsigned int SIGN_BYTES = crypto_sign_BYTES;
[[maybe_unused]] static constexpr unsigned int PUB_KEY_BYTES =
    crypto_sign_PUBLICKEYBYTES;

using pub_key = deleted_unique_ptr<unsigned char>;

/**
 * Initializes this lib and creates a local keypair
 **/
void init();

/**
 * Publishes the public part of the local keypair under the key `mem_key` to
 * the central registry
 **/
void publish_pub_key(std::string mem_key);

/**
 * Gets a public key from the central registry stored und the key `mem_key`
 **/
pub_key get_public_key(std::string mem_key);

/**
 * Gets all public keys given the prefix and the remote ids
 * Keys are looked up in the central registry using "<prefix><remote_id>"
 **/
std::map<int, pub_key> get_public_keys(std::string prefix,
                                       std::vector<int>& remote_ids);

/**
 * Signs the provided message with the private key of the local keypair. The
 * signature is stored in the memory pointed to by sig.
 *
 * @param sig: pointer where to store the signature
 * @param msg: pointer to the message to sign
 * @param msg_len: length of the message
 *
 **/
int sign(unsigned char* sig, const unsigned char* msg,
         unsigned long long msg_len);

/**
 * Verifies that the signature of msg was created with the secret key matching
 * the public key `pk`.
 *
 * @param sig: pointer to the signature
 * @param msg: pointer to the message
 * @param msg_len: length of the message
 * @param pk: pointer to the pubic key
 *
 * @returns boolean indicating the success of the verification
 **/
bool verify(const unsigned char* sig, const unsigned char* msg,
            unsigned long long msg_len, const unsigned char* pk);

}  // namespace crypto
}  // namespace dory