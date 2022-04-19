#pragma once

#include <assert.h>
#include <iostream>

#include "./visitor.h"
#include "./bdd-nodes.h"
#include "./bdd.h"

namespace BDD {

class PrinterDebug : public BDDVisitor {
private:
  bool traverse;

public:
  static void debug(const Node* node) {
    PrinterDebug debug(false);
    node->visit(debug);
  }

public:
  PrinterDebug(bool _traverse) : traverse(_traverse) {}
  PrinterDebug() : PrinterDebug(true) {}

  Action visitBranch(const Branch* node) override {
    auto condition = node->get_condition();
    
    std::cerr << "===========================================" << "\n";
    std::cerr << "type:      branch" << "\n";
    std::cerr << "condition: "; condition->dump();
    std::cerr << "===========================================" << "\n";

    return traverse ? VISIT_CHILDREN : STOP;
  }

  Action visitCall(const Call* node) override {
    auto call = node->get_call();

    std::cerr << "===========================================" << "\n";
    std::cerr << "type:      call" << "\n";
    std::cerr << "callpaths: ";
    int i = 0;
    for (const auto& filename : node->get_call_paths_filenames()) {
      if (i > 0 && i % 5 == 0)  {
        std::cerr << "\n" << "           ";
      }
      if (i > 0 && i % 5 != 0) std::cerr << ", ";
      std::cerr << filename;
      i++;
    }
    std::cerr << "\n";
    std::cerr << "function:  " << call.function_name << "\n";
    std::cerr << "args:      ";
    bool indent = false;
    for (const auto& arg : call.args) {
      if (indent) std::cerr << "           ";
      std::cerr << arg.first << " : "; arg.second.expr->dump();
      indent = true;
    }
    if (!call.ret.isNull()) {
      std::cerr << "ret:       "; call.ret->dump();
    }
    std::cerr << "===========================================" << "\n";

    return traverse ? VISIT_CHILDREN : STOP;
  }

  Action visitReturnRaw(const ReturnRaw* node) override {
    auto calls_list = node->get_calls();

    std::cerr << "===========================================" << "\n";
    std::cerr << "type:      return raw" << "\n";
    std::cerr << "lcalls:    " << calls_list.size() << "\n";
    for (const auto& calls : calls_list) {
      std::cerr << "calls:     " << calls.size() << "\n";
      for (const auto& call : calls) {
        std::cerr << "call:      " << call.function_name << " " << expr_to_string(call.ret) << "\n";
      }
    }

    std::cerr << "===========================================" << "\n";

    return traverse ? VISIT_CHILDREN : STOP;
  }

  Action visitReturnInit(const ReturnInit* node) override {
    auto call_paths_filenames = node->get_call_paths_filenames();
    auto value = node->get_return_value();

    std::cerr << "===========================================" << "\n";
    std::cerr << "type:      return init" << "\n";
    std::cerr << "callpaths: ";
    int i = 0;
    for (const auto& filename : call_paths_filenames) {
      if (i > 0 && i % 5 == 0)  {
        std::cerr << "\n" << "           ";
      }
      if (i > 0 && i % 5 != 0) std::cerr << ", ";
      std::cerr << filename;
      i++;
    }
    std::cerr << "\n";
    std::cerr << "value:     ";
    switch (value) {
    case ReturnInit::ReturnType::SUCCESS: { std::cerr << "SUCCESS"; break; }
    case ReturnInit::ReturnType::FAILURE: { std::cerr << "FAILURE"; break; }
    default: { assert(false); }
    }
    std::cerr << "\n";
    std::cerr << "===========================================" << "\n";

    return traverse ? VISIT_CHILDREN : STOP;
  }

  Action visitReturnProcess(const ReturnProcess* node) override {
    auto call_paths_filenames = node->get_call_paths_filenames();
    auto value = node->get_return_value();
    auto operation = node->get_return_operation();

    std::cerr << "===========================================" << "\n";
    std::cerr << "type:      return process" << "\n";
    std::cerr << "callpaths: ";
    int i = 0;
    for (const auto& filename : call_paths_filenames) {
      if (i > 0 && i % 5 == 0)  {
        std::cerr << "\n" << "           ";
      }
      if (i > 0 && i % 5 != 0) std::cerr << ", ";
      std::cerr << filename;
      i++;
    }
    std::cerr << "\n";
    std::cerr << "operation: ";
    switch (operation) {
    case ReturnProcess::Operation::FWD:   { std::cerr << "fwd(" << value << ")"; break; }
    case ReturnProcess::Operation::DROP:  { std::cerr << "drop()"; break; }
    case ReturnProcess::Operation::BCAST: { std::cerr << "bcast()"; break; }
    case ReturnProcess::Operation::ERR:   { std::cerr << "ERR"; break; }
    }
    std::cerr << "\n";
    std::cerr << "===========================================" << "\n";

    return traverse ? VISIT_CHILDREN : STOP;
  }

  void visitInitRoot(const Node* root) override {
    std::cerr << "\n================== INIT ==================\n\n";
    root->visit(*this);
  }

  void visitProcessRoot(const Node* root) override {
    std::cerr << "\n================== PROCESS ==================\n\n";
    root->visit(*this);
  }
  
};

}
