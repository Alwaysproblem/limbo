// vim:filetype=cpp:textwidth=120:shiftwidth=2:softtabstop=2:expandtab
// Copyright 2014--2016 Christoph Schwering

#include <gtest/gtest.h>

#include <lela/term.h>
#include <lela/internal/bloom.h>
#include <lela/format/output.h>

namespace lela {
namespace internal {

using namespace lela::format;

TEST(BloomFilterTest, Subset_Contains) {
  Symbol::Factory sf;
  Term::Factory tf;
  const Symbol::Sort s1 = sf.CreateSort();
  const Symbol::Sort s2 = sf.CreateSort();
  const Term n1 = tf.CreateTerm(sf.CreateName(s1));
  const Term n2 = tf.CreateTerm(sf.CreateName(s1));
  const Term x1 = tf.CreateTerm(sf.CreateVariable(s1));
  const Term x2 = tf.CreateTerm(sf.CreateVariable(s1));
  const Symbol f = sf.CreateFunction(s1, 1);
  const Symbol g = sf.CreateFunction(s2, 1);
  const Symbol h = sf.CreateFunction(s2, 2);
  const Term f1 = tf.CreateTerm(f, {n1});
  const Term f2 = tf.CreateTerm(h, {n1,x2});
  const Term f3 = tf.CreateTerm(g, {f1});
  const Term f4 = tf.CreateTerm(h, {n1,f1});
  const std::vector<Term> ts({n1,n2,x1,x2,f1,f2,f3});

  BloomFilter bf0;
  BloomFilter bf1;

  for (Term t : ts) {
    EXPECT_TRUE(bf0.SubsetOf(bf1));
    EXPECT_FALSE(bf1.Contains(t.hash()));
  }
  for (Term t : ts) {
    EXPECT_TRUE(bf0.SubsetOf(bf1));
    EXPECT_FALSE(bf1.Contains(t.hash()));
    bf1.Add(t.hash());
    EXPECT_TRUE(bf1.Contains(t.hash()));
    EXPECT_TRUE(bf0.SubsetOf(bf1));
    //std::cout << std::hex << bf1.mask_ << std::endl;
  }

  for (Term t : ts) {
    EXPECT_TRUE(bf0.SubsetOf(bf0));
    EXPECT_FALSE(bf0.Contains(t.hash()));
  }
  for (Term t : ts) {
    EXPECT_TRUE(bf0.SubsetOf(bf0));
    EXPECT_FALSE(bf0.Contains(t.hash()));
    bf0.Add(t.hash());
    //std::cout << std::hex << bf0.mask_ << std::endl;
    EXPECT_TRUE(bf0.Contains(t.hash()));
    EXPECT_TRUE(bf0.SubsetOf(bf0));
  }

  bf0.Add(f4.hash());
  //std::cout << std::hex << bf0.mask_ << std::endl;
  EXPECT_TRUE(bf0.Contains(f4.hash()));
  EXPECT_FALSE(bf1.Contains(f4.hash()));
  EXPECT_FALSE(bf0.SubsetOf(bf1));

  bf0.Clear();
  EXPECT_TRUE(bf0.SubsetOf(bf1));
  EXPECT_FALSE(bf1.SubsetOf(bf0));
}

TEST(BloomFilterTest, hash) {
  uint64_t x = 0xFF03FF02FF01FF00;
  EXPECT_EQ(BloomFilter::hash<0>(x), 0xFF00);
  EXPECT_EQ(BloomFilter::hash<1>(x), 0xFF01);
  EXPECT_EQ(BloomFilter::hash<2>(x), 0xFF02);
  EXPECT_EQ(BloomFilter::hash<3>(x), 0xFF03);
  //std::cout << std::hex << x << std::endl;
  //std::cout << std::hex << BloomFilter::hash<0>(x) << std::endl;
  //std::cout << std::hex << BloomFilter::hash<1>(x) << std::endl;
  //std::cout << std::hex << BloomFilter::hash<2>(x) << std::endl;
  //std::cout << std::hex << BloomFilter::hash<3>(x) << std::endl;
}

}  // namespace internal
}  // namespace lela
