#pragma once

#include <iostream>

#include <gtest/gtest.h>

#include "../../src/shared/slot-tracker.hpp"

TEST(SlotTrackerTest, HasProperInitializationValues) {
  SlotTracker tracker;

  ASSERT_EQ(tracker.processed_sig, false);
  ASSERT_EQ(tracker.sig_valid, false);
}

TEST(SlotTrackerTest, HasProperQuorumSizeAfterMatchUpgrade) {
  SlotTracker tracker;

  const auto pid = 5;

  tracker.add_to_empty_reads(pid);
  ASSERT_TRUE(tracker.has_quorum_of(1));

  tracker.add_to_match_reads(pid);
  ASSERT_TRUE(tracker.has_quorum_of(1));
  ASSERT_TRUE(tracker.match_reads_include(pid));
}

TEST(PendingSlotsTest, DoesInsert) {
  PendingSlots slots;

  const auto key = 5;

  auto ref = slots.insert(key);

  ASSERT_TRUE(slots.exist(key));
  ASSERT_TRUE(slots.get(key).has_value());
}

TEST(PendingSlotsTest, DoesRemoveForMatchReadQuorumWithoutSig) {
  PendingSlots slots;

  const auto key = 5;
  const auto p1 = 1;
  const auto p2 = 2;

  auto &tracker = slots.insert(key).get();

  tracker.add_to_match_reads(p1);
  tracker.add_to_match_reads(p2);

  ASSERT_FALSE(tracker.sig_valid);
  ASSERT_TRUE(slots.remove_if_complete(key, 2));
}

TEST(PendingSlotsTest, DoesNotRemoveForEmptyReadsWithoutSig) {
  PendingSlots slots;

  const auto key = 5;

  const auto p1 = 1;
  const auto p2 = 2;

  auto &tracker = slots.insert(key).get();

  tracker.add_to_empty_reads(p1);
  tracker.add_to_empty_reads(p2);

  ASSERT_FALSE(slots.remove_if_complete(key, 2));
}

TEST(PendingSlotsTest, IsDeliverableAfterRemoveOf) {
  PendingSlots slots;

  const auto key1 = 1;
  const auto key2 = 2;
  const auto key3 = 3;

  const auto p1 = 1;
  const auto p2 = 2;
  const auto p3 = 3;

  slots.insert(key1);
  slots.insert(key2);

  auto &tracker = slots.insert(key3).get();

  tracker.add_to_match_reads(p1);
  tracker.add_to_match_reads(p2);

  auto const &ids = slots.deliverable_after_remove_of(p3, 2);

  ASSERT_EQ(ids.size(), 1);
  ASSERT_EQ(ids[0], key3);
}