#include "../../src/async/swmr.hpp"

#include <gtest/gtest.h>

TEST(SWMRRegisterTest, IsInitiallyNotComplete) {
  SWMRRegister reg;
  const auto p1 = 1;
  const auto count = 0;

  ASSERT_EQ(reg.is_complete(count), false);

  reg.add_to(count, p1);

  ASSERT_EQ(reg.is_complete(count), false);
}

TEST(SWMRRegisterTest, DoesCleanReadListWhenComplete) {
  SWMRRegister reg;
  const auto p1 = 1;
  const auto p2 = 2;
  const auto count = 0;

  reg.add_to(count, p1);
  reg.add_to(count, p2);

  ASSERT_EQ(reg.read_list_size(count), 2);

  reg.set_complete(count);

  ASSERT_EQ(reg.is_complete(count), true);

  ASSERT_EQ(reg.read_list_size(count), 0);
}