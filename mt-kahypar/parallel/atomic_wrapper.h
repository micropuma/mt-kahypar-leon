/*******************************************************************************
 * This file is part of KaHyPar.
 *
 * Copyright (C) 2019 Tobias Heuer <tobias.heuer@kit.edu>
 *
 * KaHyPar is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * KaHyPar is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with KaHyPar.  If not, see <http://www.gnu.org/licenses/>.
 *
 ******************************************************************************/
#pragma once

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Weffc++"

#include <atomic>
#include <type_traits>

namespace mt_kahypar {
namespace parallel {

template <typename T>
void fetch_add(std::atomic<T>& x, T y) {
  T cur_x = x.load();
  while (!x.compare_exchange_weak(cur_x, cur_x + y, std::memory_order_relaxed));
}

template <typename T>
void fetch_sub(std::atomic<T>& x, T y) {
  T cur_x = x.load();
  while (!x.compare_exchange_weak(cur_x, cur_x - y, std::memory_order_relaxed));
}

template <class T>
class AtomicWrapper : public std::atomic<T> {
 public:
  void operator+= (T other) {
    fetch_add(*this, other);
  }

  void operator-= (T other) {
    fetch_sub(*this, other);
  }
};

template <typename T>
class IntegralAtomicWrapper {
  static_assert(std::is_integral<T>::value, "Value must be of integral type");
  static_assert( std::atomic<T>::is_always_lock_free, "Atomic must be lock free" );

 public:
  explicit IntegralAtomicWrapper(const T value) :
    _value(value) { }

  IntegralAtomicWrapper(const IntegralAtomicWrapper& other) :
    _value(other._value.load()) { }

  IntegralAtomicWrapper & operator= (const IntegralAtomicWrapper& other) {
    _value = other._value.load();
    return *this;
  }

  IntegralAtomicWrapper(IntegralAtomicWrapper&& other) :
    _value(other._value.load()) { }

  IntegralAtomicWrapper & operator= (IntegralAtomicWrapper&& other) {
    _value = other._value.load();
    return *this;
  }

  T operator= (T desired) noexcept {
    _value = desired;
    return _value;
  }

  void store(T desired, std::memory_order order = std::memory_order_seq_cst) noexcept {
    _value.store(desired, order);
  }

  T load(std::memory_order order = std::memory_order_seq_cst) noexcept {
    return _value.load(order);
  }

  operator T () const noexcept {
    return _value.load();
  }

  T exchange(T desired, std::memory_order order = std::memory_order_seq_cst) noexcept {
    return _value.exchange(desired, order);
  }

  bool compare_and_exchange_weak(T& expected, T& desired, std::memory_order order = std::memory_order_seq_cst) noexcept {
    return _value.compare_exchange_weak(expected, desired, order);
  }

  bool compare_and_exchange_strong(T& expected, T& desired, std::memory_order order = std::memory_order_seq_cst) noexcept {
    return _value.compare_exchange_strong(expected, desired, order);
  }

  T fetch_add(T arg, std::memory_order order = std::memory_order_seq_cst) noexcept {
    return _value.fetch_add(arg, order);
  }

  T fetch_sub(T arg, std::memory_order order = std::memory_order_seq_cst) noexcept {
    return _value.fetch_sub(arg, order);
  }

  T fetch_and(T arg, std::memory_order order = std::memory_order_seq_cst) noexcept {
    return _value.fetch_and(arg, order);
  }

  T fetch_or(T arg, std::memory_order order = std::memory_order_seq_cst) noexcept {
    return _value.fetch_or(arg, order);
  }

  T fetch_xor(T arg, std::memory_order order = std::memory_order_seq_cst) noexcept {
    return _value.fetch_xor(arg, order);
  }

  T operator++ () noexcept {
    return ++_value;
  }

  T operator++ (int) noexcept {
    return _value++;
  }

  T operator-- () noexcept {
    return --_value;
  }

  T operator-- (int) noexcept {
    return _value++;
  }

  T operator+= (T arg) noexcept {
    return _value.fetch_add(arg) + arg;
  }

  T operator-= (T arg) noexcept {
    return _value.fetch_sub(arg) - arg;
  }

  T operator&= (T arg) noexcept {
    return _value.fetch_and(arg) & arg;
  }

  T operator|= (T arg) noexcept {
    return _value.fetch_or(arg) | arg;
  }

  T operator^= (T arg) noexcept {
    return _value.fetch_xor(arg) ^ arg;
  }

 private:
  std::atomic<T> _value;
};

#pragma GCC diagnostic pop
}  // namespace parallel
}  // namespace mt_kahypar