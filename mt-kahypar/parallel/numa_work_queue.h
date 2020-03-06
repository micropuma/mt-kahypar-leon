/*******************************************************************************
 * This file is part of MT-KaHyPar.
 *
 * Copyright (C) 2020 Lars Gottesbüren <lars.gottesbueren@kit.edu>
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

#include <tbb/concurrent_queue.h>
#include <vector>

#include "../definitions.h"

namespace mt_kahypar {

// TODO better name
template<typename T>
class ConcurrentDataContainer {
public:

  ConcurrentDataContainer(size_t maxNumElements) : size(0), elements(maxNumElements, T()) { }

  void push_back(const T& el) {
    const size_t old_size = size.fetch_add(1, std::memory_order_acq_rel);
    assert(old_size < vec.size());
    elements[old_size] = el;
  }

  bool try_pop(T& dest) {
    const size_t old_size = size.fetch_sub(1, std::memory_order_acq_rel);
    if (old_size > 0) {
      dest = elements[old_size - 1];
      return true;
    }
    return false;
  }

  bool empty() {
    return unsafe_size() == 0;
  }

  size_t unsafe_size() const {
    return size.load(std::memory_order_acq_rel);
  }

  vec<T>& get_underlying_container() {
    return elements;
  }

  void clear() {
    size.store(0);
  }

  void shrink_to_fit() {
    elements.resize(size);
    elements.shrink_to_fit();
  }

private:
  std::atomic<size_t> size;
  vec<T> elements;
};

template<typename Work>
class NumaWorkQueue {
public:
  explicit NumaWorkQueue(size_t numSockets, size_t maxNumElements) : queues(numSockets, ConcurrentDataContainer(maxNumElements)) { }
  explicit NumaWorkQueue(size_t maxNumElements) : NumaWorkQueue(static_cast<size_t>(TBBNumaArena::instance().num_used_numa_nodes()), maxNumElements) { }

  bool empty() const {
    return std::all_of(queues.begin(), queues.end(), [](const auto& q) { return q.empty(); });
  }

  void push(const Work& w, const int socket) {
    queues[socket].push_back(w);
  }

  bool tryPop(Work& dest, int preferredSocket) {
    if (queues[preferredSocket].try_pop(dest)) {
      return true;
    }
    size_t maxIndex = 0;
    size_t maxSize = 0;
    for (size_t i = 0; i < queues.size(); ++i) {
      size_t size = queues[i].unsafe_size();
      if (size > maxSize) {
        maxSize = size;
        maxIndex = i;
      }
    }
    return queues[maxIndex].try_pop(dest);
  }

  bool tryPop(Work& dest) {
    int socket = HardwareTopology::instance().numa_node_of_cpu(sched_getcpu());
    return tryPop(dest, socket);
  }

  size_t unsafe_size() const {
    size_t s = 0;
    for (size_t i = 0; i < queues.size(); ++i) s += queues[i].unsafe_size();
    return s;
  }

  void shuffleQueues() {
    tbb::parallel_for(0UL, queues.size(), [&](size_t i) {
      std::mt19937 rng(queues[i].unsafe_size() + i);
      auto& data = queues[i].get_underlying_container();
      std::shuffle(data.begin(), data.end(), rng);
    });
  }

private:
  vec<ConcurrentDataContainer<Work>> queues;

};

}