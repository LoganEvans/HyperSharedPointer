#include "CPUDistributedCounter.h"

#include <glog/logging.h>
#include "gtest/gtest.h"

TEST(CPUDistributedCounterTest, basic) {
  distcount::Arena arena;
  auto counter = arena.getCounter();
  printf("counter.arena(): 0x%zx\n", reinterpret_cast<size_t>(counter.arena()));

  counter.increment();
  counter.increment();
  EXPECT_TRUE(counter.decrement());
  EXPECT_FALSE(counter.decrement());
}
