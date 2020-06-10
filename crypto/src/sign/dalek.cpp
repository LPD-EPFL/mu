
#include "dalek.hpp"

#include <iostream>
#include <memory>
#include <thread>

#include <dory/shared/logger.hpp>
#include <dory/shared/pointer-wrapper.hpp>

namespace dory {
namespace crypto {
namespace dalek {

extern "C" {

typedef struct keypair keypair_t;

extern uint8_t *public_part(keypair_t *keypair);
extern keypair_t *keypair_create();
extern publickey_t *publickey_new(const uint8_t *key, size_t len);
extern keypair_t *keypair_new(const uint8_t *key, size_t len);

extern void keypair_free(keypair_t *keypair);
extern void publickey_free(publickey_t *public_key);

extern void keypair_sign_into(uint8_t *sig, keypair_t *keypair,
                              const uint8_t *msg, size_t len);

extern dory::crypto::dalek::signature keypair_sign(keypair_t *keypair,
                                                   const uint8_t *msg,
                                                   size_t len);

extern uint8_t keypair_verify(keypair_t *keypair, const uint8_t *msg,
                              size_t len, dory::crypto::dalek::signature *sig);

extern uint8_t publickey_verify_raw(publickey_t *keypair, const uint8_t *msg,
                                    size_t len, const uint8_t *raw_sig);
extern uint8_t publickey_verify(publickey_t *public_key, const uint8_t *msg,
                                size_t len,
                                dory::crypto ::dalek::signature *sig);
}

auto logger = dory::std_out_logger("CRYPTO");

deleted_unique_ptr<keypair> kp;

volatile bool initialized = false;

void init() {
  if (initialized) {
    SPDLOG_LOGGER_WARN(logger, "Trying to re-initialize dalek's library!");
    return;
  }

  initialized = true;

  auto raw_kp = keypair_create();

  kp = deleted_unique_ptr<keypair>(raw_kp,
                                   [](keypair *rkp) { keypair_free(rkp); });
}

void publish_pub_key(std::string mem_key) {
  dory::MemoryStore::getInstance().set(
      mem_key, std::string(reinterpret_cast<char *>(public_part(kp.get())),
                           PUBLIC_KEY_LENGTH));
}

pub_key get_public_key(std::string mem_key) {
  std::string ret;

  if (!dory::MemoryStore::getInstance().get(mem_key, ret)) {
    throw std::runtime_error("Key not found");
  }

  return deleted_unique_ptr<publickey>(
      publickey_new(reinterpret_cast<uint8_t *>(ret.data()), PUBLIC_KEY_LENGTH),
      [](publickey *rpk) { publickey_free(rpk); });
}

std::map<int, pub_key> get_public_keys(std::string prefix,
                                       std::vector<int> &remote_ids) {
  std::map<int, pub_key> remote_keys;

  for (int pid : remote_ids) {
    auto memkey = prefix + std::to_string(pid);
    while (true) {
      try {
        remote_keys.insert(
            std::pair<int, pub_key>(pid, get_public_key(memkey)));
        break;
      } catch (...) {
        logger->info("{} not pushlished yet", memkey);
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
      }
    }
  }

  return remote_keys;
}

signature sign(const unsigned char *msg, unsigned long long msg_len) {
  return keypair_sign(kp.get(), msg, msg_len);
}

void sign(unsigned char *buf, const unsigned char *msg,
          unsigned long long msg_len) {
  return keypair_sign_into(buf, kp.get(), msg, msg_len);
}

bool verify(signature &sig, const unsigned char *msg,
            unsigned long long msg_len, pub_key &pk) {
  return publickey_verify(pk.get(), msg, msg_len, &sig);
}

bool verify(const unsigned char *sig, const unsigned char *msg,
            unsigned long long msg_len, pub_key &pk) {
  return publickey_verify_raw(pk.get(), msg, msg_len,
                              reinterpret_cast<const uint8_t *>(sig));
}

}  // namespace dalek
}  // namespace crypto
}  // namespace dory