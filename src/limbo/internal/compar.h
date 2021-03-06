// vim:filetype=cpp:textwidth=120:shiftwidth=2:softtabstop=2:expandtab
// Copyright 2014 Christoph Schwering
// Licensed under the MIT license. See LICENSE file in the project root.
//
// A couple of comparators to achieve specific behaviour of set and map
// containers. Currently only few are in use, the previous prototypes made
// much heavier use of specific sorting.

#ifndef LIMBO_INTERNAL_COMPAR_H_
#define LIMBO_INTERNAL_COMPAR_H_

#include <algorithm>

namespace limbo {
namespace internal {

template<typename T>
struct LessComparator {
  typedef T value_type;
  bool operator()(const T& t1, const T& t2) const {
    return t1 < t2;
  }
};

template<typename T, typename Compar = typename T::key_compare>
struct LexicographicContainerComparator {
  typedef T value_type;
  bool operator()(const T& t1, const T& t2) const {
    return std::lexicographical_compare(t1.begin(), t1.end(),
                                        t2.begin(), t2.end(),
                                        comp);
  }

 private:
  Compar comp;
};

template<typename T>
struct BySizeComparator {
  typedef T value_type;
  bool operator()(const T& t1, const T& t2) const {
    return t1.size() < t2.size();
  }
};

template<typename Compar, typename... Compars>
struct LexicographicComparator {
  bool operator()(const typename Compar::value_type& x,
                  const typename Compars::value_type&... xs,
                  const typename Compar::value_type& y,
                  const typename Compars::value_type&... ys) const {
    if (head_comp(x, y)) {
      return true;
    }
    if (head_comp(y, x)) {
      return false;
    }
    return tail_comp(xs..., ys...);
  }

 private:
  Compar head_comp;
  LexicographicComparator<Compars...> tail_comp;
};

template<typename Compar>
struct LexicographicComparator<Compar> {
  bool operator()(const typename Compar::value_type& x,
                  const typename Compar::value_type& y) const {
    return comp(x, y);
  }

 private:
  Compar comp;
};

}  // namespace internal
}  // namespace limbo

#endif  // LIMBO_INTERNAL_COMPAR_H_

