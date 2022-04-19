#include "llvm/Support/CommandLine.h"
#include "llvm/Support/MemoryBuffer.h"

#include "call-paths-to-bdd.h"

namespace {
llvm::cl::list<std::string> InputCallPathFiles(llvm::cl::desc("<call paths>"),
                                               llvm::cl::Positional);

llvm::cl::OptionCategory BDDGeneratorCat("BDD generator specific options");

llvm::cl::opt<std::string>
    Gv("gv", llvm::cl::desc("GraphViz file for BDD visualization."),
       llvm::cl::cat(BDDGeneratorCat));

llvm::cl::opt<std::string>
    InputBDDFile("in", llvm::cl::desc("Input file for BDD deserialization."),
                 llvm::cl::cat(BDDGeneratorCat));

llvm::cl::opt<std::string>
    OutputBDDFile("out", llvm::cl::desc("Output file for BDD serialization."),
                  llvm::cl::cat(BDDGeneratorCat));
} // namespace

int main(int argc, char **argv) {
  llvm::cl::ParseCommandLineOptions(argc, argv);

  if (InputBDDFile.size()) {
    auto bdd = BDD::BDD::deserialize(InputBDDFile);

    if (Gv.size()) {
      auto file = std::ofstream(Gv);
      assert(file.is_open());

      BDD::GraphvizGenerator graphviz_generator(file);
      bdd.visit(graphviz_generator);
    }

    return 0;
  }

  std::vector<call_path_t *> call_paths;

  if (InputCallPathFiles.size() == 0) {
    assert(false &&
           "Please provide either at least 1 call path file, or a bdd file");
  }

  for (auto file : InputCallPathFiles) {
    std::cerr << "Loading: " << file << std::endl;

    std::vector<std::string> expressions_str;
    std::deque<klee::ref<klee::Expr>> expressions;

    call_path_t *call_path = load_call_path(file, expressions_str, expressions);
    call_paths.push_back(call_path);
  }

  BDD::BDD bdd(call_paths);

  BDD::PrinterDebug printer;
  bdd.visit(printer);

  if (Gv.size()) {
    auto file = std::ofstream(Gv);
    assert(file.is_open());

    BDD::GraphvizGenerator graphviz_generator(file);
    bdd.visit(graphviz_generator);
  }

  if (OutputBDDFile.size()) {
    BDD::BDD::serialize(bdd, OutputBDDFile);
  }

  for (auto call_path : call_paths) {
    delete call_path;
  }

  return 0;
}
