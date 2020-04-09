#include <dory/crypto/sign.hpp>
#include <dory/shared/logger.hpp>
#include <dory/store.hpp>

auto logger = dory::std_out_logger("MAIN");

/**
 * NOTE:  For this to successfully run, you need to have memcached running.
 *        Refer to the memstore package for further information.
 * */
int main() {
  logger->info("Creating and publishing key and verifying own signature");

  dory::crypto::init();

  dory::crypto::publish_pub_key("p1-pk");

  unsigned char sig[dory::crypto::SIGN_BYTES];

  char msg[] = "HELLO WORLD";
  unsigned long long msg_len = 12;

  dory::crypto::sign(sig, reinterpret_cast<unsigned char*>(msg), msg_len);

  auto pk = dory::crypto::get_public_key("p1-pk");

  assert(dory::crypto::verify(const_cast<const unsigned char*>(sig),
                              reinterpret_cast<unsigned char*>(msg), msg_len,
                              pk.get()));

  logger->info("Testing finished successfully!");

  return 0;
}