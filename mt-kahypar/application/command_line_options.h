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

#include <boost/program_options.hpp>

#if defined(_MSC_VER)
#include <Windows.h>
#include <process.h>
#else
#include <sys/ioctl.h>
#endif

#include <cctype>
#include <limits>
#include <string>
#include <vector>
#include <fstream>

#include "mt-kahypar/mt_kahypar.h"
#include "mt-kahypar/partition/context.h"
#include "mt-kahypar/io/partitioning_output.h"

namespace po = boost::program_options;

namespace mt_kahypar {
namespace platform {
int getTerminalWidth() {
  int columns = 0;
#if defined(_MSC_VER)
  CONSOLE_SCREEN_BUFFER_INFO csbi;
  GetConsoleScreenBufferInfo(GetStdHandle(STD_OUTPUT_HANDLE), &csbi);
  columns = csbi.srWindow.Right - csbi.srWindow.Left + 1;
#else
  struct winsize w = { };
  ioctl(0, TIOCGWINSZ, &w);
  columns = w.ws_col;
#endif
  return columns;
}

int getProcessID() {
#if defined(_MSC_VER)
  return _getpid();
#else
  return getpid();
#endif
}
}  // namespace platform

po::options_description createGeneralOptionsDescription(Context& context, const int num_columns) {
  po::options_description options("General Options", num_columns);
  options.add_options()
    ("seed",
    po::value<int>(&context.partition.seed)->value_name("<int>"),
    "Seed for random number generator \n"
    "(default: -1)")
    ("cmaxnet",
    po::value<HyperedgeID>(&context.partition.hyperedge_size_threshold)->value_name("<uint32_t>"),
    "Hyperedges larger than cmaxnet are ignored during partitioning process.")
    ("objective,o",
    po::value<std::string>()->value_name("<string>")->required()->notifier([&](const std::string& s) {
      if (s == "cut") {
        context.partition.objective = Objective::cut;
      } else if (s == "km1") {
        context.partition.objective = Objective::km1;
      }
    }),
    "Objective: \n"
    " - cut : cut-net metric \n"
    " - km1 : (lambda-1) metric")
    ("mode,m",
    po::value<std::string>()->value_name("<string>")->required()->notifier(
      [&](const std::string& mode) {
      context.partition.mode = kahypar::modeFromString(mode);
    }),
    "Partitioning mode: \n"
    " - (recursive) bisection \n"
    " - (direct) k-way");
  return options;
}

po::options_description createGenericOptionsDescription(Context& context,
                                                        const int num_columns) {
  po::options_description generic_options("Generic Options", num_columns);
  generic_options.add_options()
    ("help", "show help message")
    ("verbose,v", po::value<bool>(&context.partition.verbose_output)->value_name("<bool>"),
    "Verbose main partitioning output")
    ("quiet,q", po::value<bool>(&context.partition.quiet_mode)->value_name("<bool>"),
    "Quiet Mode: Completely suppress console output")
    ("show-detailed-timings", po::value<bool>(&context.partition.detailed_timings)->value_name("<bool>"),
    "If true, detailed timings overview is shown")
    ("time-limit", po::value<int>(&context.partition.time_limit)->value_name("<int>"),
    "Time limit in seconds")
    ("sp-process,s", po::value<bool>(&context.partition.sp_process_output)->value_name("<bool>"),
    "Summarize partitioning results in RESULT line compatible with sqlplottools "
    "(https://github.com/bingmann/sqlplottools)");
  return generic_options;
}

po::options_description createCoarseningOptionsDescription(Context& context,
                                                           const int num_columns) {
  po::options_description options("Coarsening Options", num_columns);
  options.add_options()
    ("c-type",
    po::value<std::string>()->value_name("<string>")->notifier(
      [&](const std::string& ctype) {
      context.coarsening.algorithm = mt_kahypar::coarseningAlgorithmFromString(ctype);
    }),
    "Coarsening Algorithm:\n"
    " - community_coarsener")
    ("c-s",
    po::value<double>(&context.coarsening.max_allowed_weight_multiplier)->value_name("<double>"),
    "The maximum weight of a vertex in the coarsest hypergraph H is:\n"
    "(s * w(H)) / (t * k)\n")
    ("c-t",
    po::value<HypernodeID>(&context.coarsening.contraction_limit_multiplier)->value_name("<int>"),
    "Coarsening stops when there are no more than t * k hypernodes left")
    ("c-use-hypernode-degree-threshold",
    po::value<bool>(&context.coarsening.use_hypernode_degree_threshold)->value_name("<bool>"),
    "If true, than all hypernodes with a degree greater than mean + 5 * stdev are skipped during coarsening")
    ("c-rating-score",
    po::value<std::string>()->value_name("<string>")->notifier(
      [&](const std::string& rating_score) {
        context.coarsening.rating.rating_function =
          mt_kahypar::ratingFunctionFromString(rating_score);
    }), "Rating function used to calculate scores for vertex pairs:\n"
    "- heavy_edge")
    ("c-rating-heavy-node-penalty",
    po::value<std::string>()->value_name("<string>")->notifier(
      [&](const std::string& penalty) {
        context.coarsening.rating.heavy_node_penalty_policy =
          heavyNodePenaltyFromString(penalty);
    }),
    "Penalty function to discourage heavy vertices:\n"
    "- multiplicative\n"
    "- no_penalty\n"
    "- edge_frequency_penalty")
    ("c-rating-acceptance-criterion",
    po::value<std::string>()->value_name("<string>")->notifier(
      [&](const std::string& crit) {
        context.coarsening.rating.acceptance_policy =
          acceptanceCriterionFromString(crit);
    }),
    "Acceptance/Tiebreaking criterion for contraction partners having the same score:\n"
    "- best\n"
    "- best_prefer_unmatched");
  return options;
}

po::options_description createInitialPartitioningOptionsDescription(Context& context, const int num_columns) {
  po::options_description options("Initial Partitioning Options", num_columns);
  options.add_options()
    ("i-context-file",
    po::value<std::string>(&context.initial_partitioning.context_file)->required()->value_name("<string>"),
    "Context file for initial partitioning call to KaHyPar.")
    ("i-call-kahypar-multiple-times",
    po::value<bool>(&context.initial_partitioning.call_kahypar_multiple_times)->value_name("<bool>"),
    "If true, KaHyPar is called i-runs times during IP (with one call to IP of KaHyPar).\n"
    "Otherwise, KaHyPar is called s-num-threads times and the IP of KaHyPar is called i-runs times\n"
    "(splitted over s-num-threads)"
    "(default: false)")
    ("i-runs",
    po::value<size_t>(&context.initial_partitioning.runs)->value_name("<size_t>"),
    "Number of runs for initial partitioner \n"
    "(default: 1)");
  return options;
}

po::options_description createRefinementOptionsDescription(Context& context, const int num_columns) {
  po::options_description options("Refinement Options", num_columns);
  options.add_options()
    ("r-lp-type",
    po::value<std::string>()->value_name("<string>")->notifier(
      [&](const std::string& type) {
        context.refinement.label_propagation.algorithm =
          labelPropagationAlgorithmFromString(type);
    }),
    "Algorithm used for label propagation:\n"
    "- label_propagation_km1\n"
    "- label_propagation_cut\n"
    "- do_nothing")
    ("r-lp-maximum-iterations",
    po::value<size_t>(&context.refinement.label_propagation.maximum_iterations)->value_name("<size_t>"),
    "Maximum number of iterations over all nodes during label propagation\n"
    "(default 1)")
    ("r-lp-part-weight-update-frequency",
    po::value<size_t>(&context.refinement.label_propagation.part_weight_update_frequency)->value_name("<size_t>"),
    "Determines after how many iterations the local part weights are updated\n"
    "(default 100)")
    ("r-lp-use-node-degree-ordering",
    po::value<bool>(&context.refinement.label_propagation.use_node_degree_ordering)->value_name("<bool>"),
    "If true, nodes are sorted in increasing order of their node degree before LP, otherwise they are random shuffled\n"
    "(default false)")
    ("r-lp-execution-policy",
    po::value<std::string>()->value_name("<string>")->notifier(
      [&](const std::string& type) {
        context.refinement.label_propagation.execution_policy =
          executionTypeFromString(type);
    }),
    "Execution policy used for label propagation:\n"
    "- exponential\n"
    "- multilevel\n");
  return options;
}

po::options_description createSharedMemoryOptionsDescription(Context& context,
                                                             const int num_columns) {
  po::options_description shared_memory_options("Shared Memory Options", num_columns);
  shared_memory_options.add_options()
    ("s-num-threads",
    po::value<size_t>(&context.shared_memory.num_threads)->value_name("<size_t>"),
    "Number of threads used during shared memory hypergraph partitioning\n"
    "(default 1)")
    ("s-enable-community-redistribution", po::value<bool>(&context.shared_memory.use_community_redistribution)->value_name("<bool>"),
    "If true, hypergraph is redistributed based on community detection")
    ("s-community-assignment-objective",
    po::value<std::string>()->value_name("<string>")->notifier(
      [&](const std::string& objective) {
      context.shared_memory.assignment_objective = mt_kahypar::communityAssignmentObjectiveFromString(objective);
    }),
    "Objective used during community redistribution of hypergraph: \n"
    " - vertex_objective \n"
    " - pin_objective")
    ("s-community-assignment-strategy",
    po::value<std::string>()->value_name("<string>")->notifier(
      [&](const std::string& strategy) {
      context.shared_memory.assignment_strategy = mt_kahypar::communityAssignmentStrategyFromString(strategy);
    }),
    "Strategy used during community redistribution of hypergraph: \n"
    " - bin_packing");

  return shared_memory_options;
}

void processCommandLineInput(Context& context, int argc, char* argv[]) {
  const int num_columns = platform::getTerminalWidth();

  po::options_description generic_options = createGenericOptionsDescription(context, num_columns);

  po::options_description required_options("Required Options", num_columns);
  required_options.add_options()
    ("hypergraph,h",
    po::value<std::string>(&context.partition.graph_filename)->value_name("<string>")->required(),
    "Hypergraph filename")
    ("blocks,k",
    po::value<PartitionID>(&context.partition.k)->value_name("<int>")->required(),
    "Number of blocks")
    ("epsilon,e",
    po::value<double>(&context.partition.epsilon)->value_name("<double>")->required(),
    "Imbalance parameter epsilon");

  std::string context_path;
  po::options_description preset_options("Preset Options", num_columns);
  preset_options.add_options()
    ("preset,p", po::value<std::string>(&context_path)->value_name("<string>"),
    "Context Presets (see config directory):\n"
    " - <path-to-custom-ini-file>");

  po::options_description general_options = createGeneralOptionsDescription(context, num_columns);


  po::options_description coarsening_options =
    createCoarseningOptionsDescription(context, num_columns);
  po::options_description initial_paritioning_options =
    createInitialPartitioningOptionsDescription(context, num_columns);
  po::options_description refinement_options =
    createRefinementOptionsDescription(context, num_columns);
  po::options_description shared_memory_options =
    createSharedMemoryOptionsDescription(context, num_columns);

  po::options_description cmd_line_options;
  cmd_line_options.add(generic_options)
                  .add(required_options)
                  .add(preset_options)
                  .add(general_options)
                  .add(coarsening_options)
                  .add(initial_paritioning_options)
                  .add(refinement_options)
                  .add(shared_memory_options);

  po::variables_map cmd_vm;
  po::store(po::parse_command_line(argc, argv, cmd_line_options), cmd_vm);

  // placing vm.count("help") here prevents required attributes raising an
  // error if only help was supplied
  if (cmd_vm.count("help") != 0 || argc == 1) {
    mt_kahypar::io::printBanner(context);
    LOG << cmd_line_options;
    exit(0);
  }

  po::notify(cmd_vm);

  std::ifstream file(context_path.c_str());
  if (!file) {
    std::cerr << "Could not load context file at: " << context_path << std::endl;
    std::exit(-1);
  }

  po::options_description ini_line_options;
  ini_line_options.add(general_options)
                  .add(coarsening_options)
                  .add(initial_paritioning_options)
                  .add(refinement_options)
                  .add(shared_memory_options);

  po::store(po::parse_config_file(file, ini_line_options, true), cmd_vm);
  po::notify(cmd_vm);


  std::string epsilon_str = std::to_string(context.partition.epsilon);
  epsilon_str.erase(epsilon_str.find_last_not_of('0') + 1, std::string::npos);

  context.partition.graph_partition_filename =
    context.partition.graph_filename
    + ".part"
    + std::to_string(context.partition.k)
    + ".epsilon"
    + epsilon_str
    + ".seed"
    + std::to_string(context.partition.seed)
    + ".KaHyPar";
  context.partition.graph_community_filename =
    context.partition.graph_filename + ".community";
}

} // namespace mt_kahypar