// Copyright (c) YugaByte, Inc.
//
// Licensed under the Apache License, Version 2.0 (the "License"); you may not use this file except
// in compliance with the License.  You may obtain a copy of the License at
//
// http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software distributed under the License
// is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express
// or implied.  See the License for the specific language governing permissions and limitations
// under the License.
//

#pragma once

// Implementation of std functions we want to use, but cannot until we switch to newer C++.

namespace yb {

// cmp_* code is based on examples from https://en.cppreference.com/w/cpp/utility/intcmp

template <class T, class U>
constexpr std::enable_if_t<std::is_signed<T>::value == std::is_signed<U>::value, bool> cmp_equal(
    const T& t, const U& u) noexcept {
  return t == u;
}

template <class T, class U>
constexpr std::enable_if_t<std::is_signed<T>::value && !std::is_signed<U>::value, bool> cmp_equal(
    const T& t, const U& u) noexcept {
  using UT = std::make_unsigned_t<T>;
  return t < 0 ? false : UT(t) == u;
}

template <class T, class U>
constexpr std::enable_if_t<!std::is_signed<T>::value && std::is_signed<U>::value, bool> cmp_equal(
    const T& t, const U& u) noexcept {
  using UU = std::make_unsigned_t<U>;
  return u < 0 ? false : t == UU(u);
}

template <class T, class U>
constexpr std::enable_if_t<std::is_signed<T>::value == std::is_signed<U>::value, bool> cmp_less(
    const T& t, const U& u) noexcept {
  return t < u;
}

template <class T, class U>
constexpr std::enable_if_t<std::is_signed<T>::value && !std::is_signed<U>::value, bool> cmp_less(
    const T& t, const U& u) noexcept {
  using UT = std::make_unsigned_t<T>;
  return t < 0 ? true : UT(t) < u;
}

template <class T, class U>
constexpr std::enable_if_t<!std::is_signed<T>::value && std::is_signed<U>::value, bool> cmp_less(
    const T& t, const U& u) noexcept {
  using UU = std::make_unsigned_t<U>;
  return u < 0 ? false : t < UU(u);
}

template <class T, class U>
constexpr bool cmp_not_equal(const T& t, const U& u) noexcept {
  return !::yb::cmp_equal(t, u);
}

template <class T, class U>
constexpr bool cmp_greater(const T& t, const U& u) noexcept {
  return ::yb::cmp_less(u, t);
}

template <class T, class U>
constexpr bool cmp_less_equal(const T& t, const U& u) noexcept {
  return !::yb::cmp_greater(t, u);
}

template <class Pq>
class ReverseHeapToVectorHelper {
 public:
  explicit ReverseHeapToVectorHelper(Pq& heap) : heap_(heap) {}

  template <class Container>
  operator Container() const {
    Container result;
    result.resize(heap_.size());
    size_t index = heap_.size();
    while (!heap_.empty()) {
      result[--index] = heap_.top();
      heap_.pop();
    }
    return result;
  }
 private:
  Pq& heap_;
};

template <class Pq>
ReverseHeapToVectorHelper<Pq> ReverseHeapToVector(Pq& pq) {
  return ReverseHeapToVectorHelper<Pq>(pq);
}

} // namespace yb
