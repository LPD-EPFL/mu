#include <dory/crypto/sign.hpp>
#include <dory/shared/bench.hpp>
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

  static constexpr unsigned long long len = 64;
  char msg[len];

  msg[0] = 'a';

  {
    dory::BenchTimer timer("sign", true);
    dory::crypto::sign(sig, reinterpret_cast<unsigned char*>(msg), len);
  }

  auto pk = dory::crypto::get_public_key("p1-pk");

  for (int i = 0; i < 200000; i++) {
    dory::BenchTimer timer("verify", true);
    assert(dory::crypto::verify(const_cast<const unsigned char*>(sig),
                                reinterpret_cast<unsigned char*>(msg), len,
                                pk.get()));
  }

  logger->info("Testing finished successfully!");

  return 0;
}