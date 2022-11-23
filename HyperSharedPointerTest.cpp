#include "HyperSharedPointer.h"

#include <glog/logging.h>
#include "gtest/gtest.h"

TEST(HyperSharedPointerTest, counterBasic) {
  auto counter1 = hsp::ArenaManager::getInstance().getCounter();
  auto counter2{counter1};

  EXPECT_FALSE(counter1.destroy());
  EXPECT_TRUE(counter2.destroy());
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
  hsp::HyperSharedPointer<int> p1{new int};
  *p1 = 1;
  hsp::HyperSharedPointer<int> p2{new int};
  *p2 = 2;

  EXPECT_EQ(*p1, 1);
  EXPECT_EQ(*p2, 2);

  p1.swap(p2);
  EXPECT_EQ(*p1, 2);
  EXPECT_EQ(*p2, 1);
}
