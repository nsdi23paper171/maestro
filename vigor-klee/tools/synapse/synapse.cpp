#include "klee/ExprBuilder.h"
#include "klee/perf-contracts.h"
#include "klee/util/ArrayCache.h"
#include "klee/util/ExprSMTLIBPrinter.h"
#include "klee/util/ExprVisitor.h"
#include "llvm/Support/CommandLine.h"
#include <klee/Constraints.h>
#include <klee/Solver.h>

#include <algorithm>
#include <dlfcn.h>
#include <expr/Parser.h>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <memory>
#include <regex>
#include <stack>
#include <utility>
#include <vector>

#include "call-paths-to-bdd.h"
#include "load-call-paths.h"

#include "code_generator.h"
#include "execution_plan/execution_plan.h"
#include "execution_plan/visitors/graphviz.h"
#include "heuristics/heuristics.h"
#include "log.h"
#include "modules/modules.h"
#include "search.h"

namespace {
llvm::cl::list<std::string> InputCallPathFiles(llvm::cl::desc("<call paths>"),
                                               llvm::cl::Positional);

llvm::cl::OptionCategory SyNAPSE("SyNAPSE specific options");

llvm::cl::list<synapse::Target>
    TargetList(llvm::cl::desc("Available targets:"), llvm::cl::Required,
               llvm::cl::OneOrMore,
               llvm::cl::values(
                   clEnumValN(synapse::Target::x86, "x86", "x86"),
                   clEnumValN(synapse::Target::BMv2SimpleSwitchgRPC, "bmv2",
                              "P4 (BMv2 Simple Switch with gRPC)"),
                   clEnumValN(synapse::Target::FPGA, "fpga", "VeriLog (FPGA)"),
                   clEnumValN(synapse::Target::Netronome, "netronome",
                              "Micro C (Netronome)"),
                   clEnumValN(synapse::Target::Tofino, "tofino", "P4 (Tofino)"),
                   clEnumValEnd),
               llvm::cl::cat(SyNAPSE));

llvm::cl::opt<std::string>
    InputBDDFile("in", llvm::cl::desc("Input file for BDD deserialization."),
                 llvm::cl::cat(SyNAPSE));

llvm::cl::opt<std::string>
    Out("out", llvm::cl::desc("Output directory for every generated file."),
        llvm::cl::cat(SyNAPSE));
} // namespace

BDD::BDD build_bdd() {
  assert((InputBDDFile.size() != 0 || InputCallPathFiles.size() != 0) &&
         "Please provide either at least 1 call path file, or a bdd file");

  if (InputBDDFile.size() > 0) {
    return BDD::BDD::deserialize(InputBDDFile);
  }

  std::vector<call_path_t *> call_paths;

  for (auto file : InputCallPathFiles) {
    std::cerr << "Loading: " << file << std::endl;

    std::vector<std::string> expressions_str;
    std::deque<klee::ref<klee::Expr>> expressions;

    call_path_t *call_path = load_call_path(file, expressions_str, expressions);
    call_paths.push_back(call_path);
  }

  return BDD::BDD(call_paths);
}

int main(int argc, char **argv) {
  llvm::cl::ParseCommandLineOptions(argc, argv);

  synapse::Log::MINIMUM_LOG_LEVEL = synapse::Log::Level::DEBUG;
  BDD::BDD bdd = build_bdd();

  synapse::SearchEngine search_engine(bdd);
  synapse::CodeGenerator code_generator(Out);

  for (unsigned i = 0; i != TargetList.size(); ++i) {
    search_engine.add_target(TargetList[i]);
    code_generator.add_target(TargetList[i]);
  }

  synapse::Biggest biggest;
  synapse::DFS dfs;
  synapse::MostCompact most_compact;
  synapse::LeastReordered least_reordered;
  synapse::MaximizeSwitchNodes maximize_switch_nodes;

  // auto winner = search_engine.search(biggest);
  // auto winner = search_engine.search(least_reordered);
  // auto winner = search_engine.search(dfs);
  // auto winner = search_engine.search(most_compact);
  auto winner = search_engine.search(maximize_switch_nodes);

  code_generator.generate(winner);

  return 0;
}
