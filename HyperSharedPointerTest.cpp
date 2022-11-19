#include "HyperSharedPointer.h"

#include <glog/logging.h>
#include "gtest/gtest.h"

TEST(CpuDistributedCounterTest, basic) {
  hsp::Arena arena;
  auto counter = arena.getCounter();

  counter.increment();
  counter.increment();
  EXPECT_TRUE(counter.decrement());
  EXPECT_FALSE(counter.decrement());
}
