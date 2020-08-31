#pragma once

#include "mt-kahypar/partition/refinement/fm/fm_commons.h"


namespace mt_kahypar {

class FMDetails {
public:

  static constexpr bool uses_gain_cache = true;

  FMDetails(const Context& context,
            HypernodeID numNodes,
            FMSharedData& sharedData,
            FMStats& runStats) :
      context(context),
      runStats(runStats),
      sharedData(sharedData),
      blockPQ(static_cast<size_t>(context.partition.k)),
      vertexPQs(static_cast<size_t>(context.partition.k),
                VertexPriorityQueue(sharedData.vertexPQHandles.data(), numNodes))
      { }

  template<typename PHG>
  MT_KAHYPAR_ATTRIBUTE_ALWAYS_INLINE
  void insertIntoPQ(const PHG& phg, const HypernodeID v) {
    const PartitionID pv = phg.partID(v);
    auto [target, gain] = bestDestinationBlock(phg, v);
    sharedData.targetPart[v] = target;
    vertexPQs[pv].insert(v, gain);  // blockPQ updates are done later, collectively.
    runStats.pushes++;
  }

  template<typename PHG>
  MT_KAHYPAR_ATTRIBUTE_ALWAYS_INLINE
  void updateGain(const PHG& phg, const HypernodeID v, const Move& move) {
    const PartitionID pv = phg.partID(v);
    ASSERT(vertexPQs[pv].contains(v));
    const PartitionID designatedTargetV = sharedData.targetPart[v];
    Gain gain = 0;
    PartitionID newTarget = kInvalidPartition;

    if (phg.k() < 4 || designatedTargetV == move.from || designatedTargetV == move.to) {
      // moveToPenalty of designatedTargetV is affected.
      // and may now be greater than that of other blocks --> recompute full
      std::tie(newTarget, gain) = bestDestinationBlock(phg, v);
    } else {
      // moveToPenalty of designatedTargetV is not affected.
      // only move.from and move.to may be better
      std::tie(newTarget, gain) = bestOfThree(phg, v, pv, { designatedTargetV, move.from, move.to });
    }

    sharedData.targetPart[v] = newTarget;
    vertexPQs[pv].adjustKey(v, gain);
  }

  template<typename PHG>
  MT_KAHYPAR_ATTRIBUTE_ALWAYS_INLINE
  bool findNextMove(const PHG& phg, Move& m) {
    if (blockPQ.empty()) {
      return false;
    }
    while (true) {
      const PartitionID from = blockPQ.top();
      const HypernodeID u = vertexPQs[from].top();
      const Gain estimated_gain = vertexPQs[from].topKey();
      assert(estimated_gain == blockPQ.topKey());
      auto [to, gain] = bestDestinationBlock(phg, u);
      if (gain >= estimated_gain) { // accept any gain that is at least as good
        m.node = u; m.to = to; m.from = from;
        m.gain = gain;
        runStats.extractions++;
        vertexPQs[from].deleteTop();  // blockPQ updates are done later, collectively.
        return true;
      } else {
        runStats.retries++;
        vertexPQs[from].adjustKey(u, gain);
        if (vertexPQs[from].topKey() != blockPQ.keyOf(from)) {
          blockPQ.adjustKey(from, vertexPQs[from].topKey());
        }
      }
    }
  }

  void clearPQs(const size_t /* bestImprovementIndex */ ) {
    // release all nodes that were not moved
    const bool release = sharedData.release_nodes
                         && context.refinement.fm.algorithm == FMAlgorithm::fm_multitry
                         && runStats.moves > 0;

    if (release) {
      // Release all nodes contained in PQ
      for (PartitionID i = 0; i < context.partition.k; ++i) {
        for (PosT j = 0; j < vertexPQs[i].size(); ++j) {
          const HypernodeID v = vertexPQs[i].at(j);
          sharedData.nodeTracker.releaseNode(v);
        }
      }
    }

    for (PartitionID i = 0; i < context.partition.k; ++i) {
      vertexPQs[i].clear();
    }
    blockPQ.clear();
  }

  MT_KAHYPAR_ATTRIBUTE_ALWAYS_INLINE
  void updatePQs() {
    for (PartitionID i = 0; i < context.partition.k; ++i) {
      updateBlock(i);
    }
  }

  // We're letting the FM details implementation decide what happens here, since some may not want to do gain cache updates,
  // but rather update gains in their PQs or something

  template<typename PHG>
  MT_KAHYPAR_ATTRIBUTE_ALWAYS_INLINE
  void deltaGainUpdates(PHG& phg, const HyperedgeID he, const HyperedgeWeight edge_weight,
                        const PartitionID from, const HypernodeID pin_count_in_from_part_after,
                        const PartitionID to, const HypernodeID pin_count_in_to_part_after) {
    phg.gainCacheUpdate(he, edge_weight, from, pin_count_in_from_part_after, to, pin_count_in_to_part_after);
  }

private:

  MT_KAHYPAR_ATTRIBUTE_ALWAYS_INLINE
  void updateBlock(PartitionID i) {
    if (!vertexPQs[i].empty()) {
      blockPQ.insertOrAdjustKey(i, vertexPQs[i].topKey());
    } else if (blockPQ.contains(i)) {
      blockPQ.remove(i);
    }
  }

  template<typename PHG>
  MT_KAHYPAR_ATTRIBUTE_ALWAYS_INLINE
  std::pair<PartitionID, HyperedgeWeight> bestDestinationBlock(const PHG& phg,
                                                               const HypernodeID u) {
    const HypernodeWeight wu = phg.nodeWeight(u);
    const PartitionID from = phg.partID(u);
    const HypernodeWeight from_weight = phg.partWeight(from);
    PartitionID to = kInvalidPartition;
    HyperedgeWeight to_penalty = std::numeric_limits<HyperedgeWeight>::max();
    HypernodeWeight best_to_weight = from_weight - wu;
    for (PartitionID i = 0; i < phg.k(); ++i) {
      if (i != from) {
        const HypernodeWeight to_weight = phg.partWeight(i);
        const HyperedgeWeight penalty = phg.moveToPenalty(u, i);
        if ( ( penalty < to_penalty || ( penalty == to_penalty && to_weight < best_to_weight ) ) &&
             to_weight + wu <= context.partition.max_part_weights[i] ) {
          to_penalty = penalty;
          to = i;
          best_to_weight = to_weight;
        }
      }
    }
    const Gain gain = to != kInvalidPartition ? phg.moveFromBenefit(u) - to_penalty
                                              : std::numeric_limits<HyperedgeWeight>::min();
    return std::make_pair(to, gain);
  }

  template<typename PHG>
  MT_KAHYPAR_ATTRIBUTE_ALWAYS_INLINE
  std::pair<PartitionID, HyperedgeWeight> bestOfThree(const PHG& phg, HypernodeID u, PartitionID from,
                                                      std::array<PartitionID, 3> parts) {

    const HypernodeWeight wu = phg.nodeWeight(u);
    const HypernodeWeight from_weight = phg.partWeight(from);
    PartitionID to = kInvalidPartition;
    HyperedgeWeight to_penalty = std::numeric_limits<HyperedgeWeight>::max();
    HypernodeWeight best_to_weight = from_weight - wu;
    for (PartitionID i : parts) {
      if (i != from && i != kInvalidPartition) {
        const HypernodeWeight to_weight = phg.partWeight(i);
        const HyperedgeWeight penalty = phg.moveToPenalty(u, i);
        if ( ( penalty < to_penalty || ( penalty == to_penalty && to_weight < best_to_weight ) ) &&
             to_weight + wu <= context.partition.max_part_weights[i] ) {
          to_penalty = penalty;
          to = i;
          best_to_weight = to_weight;
        }
      }
    }
    const Gain gain = to != kInvalidPartition ? phg.moveFromBenefit(u) - to_penalty
                                              : std::numeric_limits<HyperedgeWeight>::min();
    return std::make_pair(to, gain);
  }

private:
  const Context& context;

  FMStats& runStats;

  FMSharedData& sharedData;

  // ! Priority Queue that contains for each block of the partition
  // ! the vertex with the best gain value
  BlockPriorityQueue blockPQ;

  // ! From PQs -> For each block it contains the vertices (contained
  // ! in that block) touched by the current local search associated
  // ! with their gain values
  vec<VertexPriorityQueue> vertexPQs;
};


}