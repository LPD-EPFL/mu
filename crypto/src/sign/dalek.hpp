#pragma once

#include <map>
#include <string>

#include <dory/shared/pointer-wrapper.hpp>
#include <dory/store.hpp>

extern "C" {
typedef struct publickey publickey_t;
}

namespace dory {
namespace crypto {
namespace dalek {

extern "C" {
extern size_t PUBLIC_KEY_LENGTH;
extern size_t SECRET_KEY_LENGTH;
extern size_t KEYPAIR_LENGTH;
extern size_t SIGNATURE_LENGTH;
}

using pub_key = deleted_unique_ptr<publickey>;

using signature = struct sig { uint8_t s[64]; };

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
 *
 * @throw std::runtime_error if no public key is associated under `mem_key`
 **/
pub_key get_public_key(std::string mem_key);

/**
 * Gets all public keys given the prefix and the remote ids
 * Keys are looked up in the central registry using "<prefix><remote_id>"
 **/
std::map<int, pub_key> get_public_keys(std::string prefix,
                                       std::vector<int> &remote_ids);

/**
 * Signs the provided message with the private key of the local keypair.
 *
 * @param msg: pointer to the message to sign
 * @param msg_len: length of the message
 * @returns the signature
 **/
signature sign(const unsigned char *msg, unsigned long long msg_len);

/**
 * Signs the provided message with the private key of the local keypair. The
 * signature is stored in the memory pointed to by sig.
 *
 * @param sig: pointer where to store the signature
 * @param msg: pointer to the message to sign
 * @param msg_len: length of the message
 *
 **/
void sign(unsigned char *buf, const unsigned char *msg,
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
bool verify(signature &sig, const unsigned char *msg,
            unsigned long long msg_len, pub_key &pk);

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
bool verify(const unsigned char *sig, const unsigned char *msg,
            unsigned long long msg_len, pub_key &pk);

}  // namespace dalek
}  // namespace crypto
}  // namespace dory