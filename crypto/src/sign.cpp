#include <memory>

#include "sign.hpp"

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
    logger->warn("Trying to re-initialize the dory-crypo library!");
    return;
  }

  initialized = true;

  sodium_init();

  crypto_sign_keypair(own_pk, own_sk);
}

void publish_pub_key(std::string mem_key) {
  dory::MemoryStore::getInstance().set(
      mem_key, std::string(reinterpret_cast<char*>(own_pk)));
}

pub_key get_public_key(std::string mem_key) {
  std::string ret;
  dory::MemoryStore::getInstance().get(mem_key, ret);

  auto* rpk =
      reinterpret_cast<unsigned char*>(malloc(crypto_sign_PUBLICKEYBYTES));

  for (unsigned int i = 0; i < crypto_sign_PUBLICKEYBYTES; i++) {
    rpk[i] = ret[i];
  }

  return deleted_unique_ptr<unsigned char>(
      rpk, [](unsigned char* data) { free(data); });
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