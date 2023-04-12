#include <glog/logging.h>
#include <rseq/rseq.h>

#include "HyperSharedPointer.h"
#include "gtest/gtest.h"

TEST(HyperSharedPointerTest, rseq) {
  ASSERT_TRUE(rseq_available(RSEQ_AVAILABLE_QUERY_KERNEL));
  ASSERT_FALSE(rseq_register_current_thread());
  ASSERT_EQ(sched_getcpu(), rseq_current_cpu());
}

TEST(HyperSharedPointerTest, defaultCtor) { hsp::HyperSharedPointer<int> p; }

TEST(HyperSharedPointerTest, ctor) { hsp::HyperSharedPointer<int> p{new int}; }

TEST(HyperSharedPointerTest, copyCtor) {
  hsp::HyperSharedPointer<int> p1{new int};
  auto p2{p1};
  EXPECT_EQ(p1, p2);
}

TEST(HyperSharedPointerTest, moveCtor) {
  hsp::HyperSharedPointer<int> p1{new int};
  auto p2{std::move(p1)};
  EXPECT_FALSE(p1);
  EXPECT_TRUE(p2);
}

TEST(HyperSharedPointerTest, assignment) {
  hsp::HyperSharedPointer<int> p1;

  {
    hsp::HyperSharedPointer<int> p2{new int};
    p1 = p2;
    EXPECT_EQ(p1.get(), p2.get());
  }
}

TEST(HyperSharedPointerTest, reset) {
  hsp::HyperSharedPointer<int> p{new int};
  EXPECT_TRUE(p);
  p.reset();
  EXPECT_FALSE(p);
}

TEST(HyperSharedPointerTest, resetValue) {
  hsp::HyperSharedPointer<int> p{new int};
  EXPECT_TRUE(p);
  p.reset(new int);
  EXPECT_TRUE(p);
}

TEST(HyperSharedPointerTest, swap) {
  hsp::HyperSharedPointer<int> p1{new int{1}};
  hsp::HyperSharedPointer<int> p2{new int{2}};

  EXPECT_EQ(*p1, 1);
  EXPECT_EQ(*p2, 2);

  p1.swap(p2);
  EXPECT_EQ(*p1, 2);
  EXPECT_EQ(*p2, 1);
}

TEST(HyperSharedPointerTest, KeepAlive) {
  hsp::HyperSharedPointer<int> p1;
  hsp::HyperSharedPointer<int> p2;

  {
    hsp::KeepAlive<int> ka{new int{4}};

    EXPECT_EQ(*ka.get(), 4);
    EXPECT_EQ(*ka.get(), 4);
    p1 = ka.get();

    EXPECT_EQ(*ka.reset(new int{5}), 5);
    EXPECT_EQ(*ka.get(), 5);
    p2 = ka.get();
  }

  EXPECT_EQ(*p1, 4);
  EXPECT_EQ(*p2, 5);
}

TEST(HyperSharedPointerTest, originalCpu) {
  hsp::HyperSharedPointer<int> p;

  EXPECT_GE(p.originalCpu(), 0);
}
