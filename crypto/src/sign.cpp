#include "sign.hpp"
#include <memory>

#include <thread>

#include <dory/shared/logger.hpp>
#include <dory/shared/pointer-wrapper.hpp>

namespace dory {
namespace crypto {

auto logger = dory::std_out_logger("CRYPTO");

unsigned char own_pk[crypto_sign_PUBLICKEYBYTES];
unsigned char own_sk[crypto_sign_SECRETKEYBYTES];

volatile bool initialized = false;

void init() {
  if (initialized) {
    SPDLOG_LOGGER_WARN(logger,
                       "Trying to re-initialize the dory-crypo library!");
    return;
  }

  initialized = true;

  sodium_init();

  crypto_sign_keypair(own_pk, own_sk);
}

void publish_pub_key(std::string mem_key) {
  dory::MemoryStore::getInstance().set(
      mem_key,
      std::string(reinterpret_cast<char*>(own_pk), crypto_sign_PUBLICKEYBYTES));
}

pub_key get_public_key(std::string mem_key) {
  std::string ret;

  if (!dory::MemoryStore::getInstance().get(mem_key, ret)) {
    throw std::runtime_error("Key not found");
  }

  auto* rpk =
      reinterpret_cast<unsigned char*>(malloc(crypto_sign_PUBLICKEYBYTES));

  ret.copy(reinterpret_cast<char*>(rpk), crypto_sign_PUBLICKEYBYTES, 0);

  return deleted_unique_ptr<unsigned char>(
      rpk, [](unsigned char* data) { free(data); });
}

std::map<int, pub_key> get_public_keys(std::string prefix,
                                       std::vector<int>& remote_ids) {
  std::map<int, pub_key> remote_keys;

  for (int pid : remote_ids) {
    auto memkey = prefix + std::to_string(pid);
    while (true) {
      try {
        remote_keys.insert(
            std::pair<int, pub_key>(pid, dory::crypto::get_public_key(memkey)));
        break;
      } catch (...) {
        logger->info("{} not pushlished yet", memkey);
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
      }
    }
  }

  return remote_keys;
}

int sign(unsigned char* sig, const unsigned char* msg,
         unsigned long long msg_len) {
  return crypto_sign_detached(sig, NULL, msg, msg_len, own_sk);
}

bool verify(const unsigned char* sig, const unsigned char* msg,
            unsigned long long msg_len, const unsigned char* pk) {
  return crypto_sign_verify_detached(sig, msg, msg_len, pk) == 0;
}

}  // namespace crypto
}  // namespace dory