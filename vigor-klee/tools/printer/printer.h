#pragma once

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
#include <sstream>
#include <stack>
#include <unordered_set>
#include <utility>
#include <vector>

std::string expr_to_string(klee::ref<klee::Expr> expr, bool one_liner = false);
std::string pretty_print_expr(klee::ref<klee::Expr> expr);

bool get_bytes_read(klee::ref<klee::Expr> expr, std::vector<unsigned> &bytes);
bool is_readLSB_complete(klee::ref<klee::Expr> expr);

class RetrieveSymbols : public klee::ExprVisitor::ExprVisitor {
private:
  std::vector<klee::ref<klee::ReadExpr>> retrieved_reads;
  std::vector<klee::ref<klee::ReadExpr>> retrieved_reads_packet_chunks;
  std::vector<klee::ref<klee::Expr>> retrieved_readLSB;
  std::unordered_set<std::string> retrieved_strings;
  bool collapse_readLSB;

public:
  RetrieveSymbols(bool _collapse_readLSB = false)
      : ExprVisitor(true), collapse_readLSB(_collapse_readLSB) {}

  klee::ExprVisitor::Action visitConcat(const klee::ConcatExpr &e) {
    klee::ref<klee::Expr> eref = const_cast<klee::ConcatExpr *>(&e);

    if (collapse_readLSB && is_readLSB_complete(eref)) {
      retrieved_readLSB.push_back(eref);
      collapse_readLSB = false;
    }

    return klee::ExprVisitor::Action::doChildren();
  }

  klee::ExprVisitor::Action visitRead(const klee::ReadExpr &e) {
    klee::UpdateList ul = e.updates;
    const klee::Array *root = ul.root;

    retrieved_strings.insert(root->name);
    retrieved_reads.emplace_back((const_cast<klee::ReadExpr *>(&e)));

    if (root->name == "packet_chunks") {
      retrieved_reads_packet_chunks.emplace_back(
          (const_cast<klee::ReadExpr *>(&e)));
    }

    return klee::ExprVisitor::Action::doChildren();
  }

  std::vector<klee::ref<klee::ReadExpr>> get_retrieved() {
    return retrieved_reads;
  }

  std::vector<klee::ref<klee::ReadExpr>> get_retrieved_packet_chunks() {
    return retrieved_reads_packet_chunks;
  }

  std::vector<klee::ref<klee::Expr>> get_retrieved_readLSB() {
    return retrieved_readLSB;
  }

  std::unordered_set<std::string> get_retrieved_strings() {
    return retrieved_strings;
  }

  static bool contains(klee::ref<klee::Expr> expr, const std::string &symbol) {
    RetrieveSymbols retriever;
    retriever.visit(expr);
    auto symbols = retriever.get_retrieved_strings();
    auto found_it = std::find(symbols.begin(), symbols.end(), symbol);
    return found_it != symbols.end();
  }
};
