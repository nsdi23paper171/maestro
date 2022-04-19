#pragma once

#include "../../../../log.h"
#include "../../../execution_plan.h"
#include "../../visitor.h"
#include "../target_code_generator.h"

#include <ctime>
#include <fstream>
#include <math.h>
#include <regex>
#include <unistd.h>

namespace synapse {

struct variable_t {
  std::string label;
  klee::ref<klee::Expr> value;
  klee::ref<klee::Expr> addr;

  variable_t(std::string _label) : label(_label) {}

  variable_t(std::string _label, klee::ref<klee::Expr> _value)
      : label(_label), value(_value) {}

  variable_t(std::string _label, klee::ref<klee::Expr> _value,
             klee::ref<klee::Expr> _addr)
      : label(_label), value(_value), addr(_addr) {}
};

typedef std::vector<variable_t> frame_t;

struct stack_t {
  std::vector<frame_t> frames;
  std::regex pattern;

  std::map<std::string, std::string> cp_var_to_code_translation;

  stack_t() : pattern("^(.*)(_\\d+?)$") {
    push();

    cp_var_to_code_translation = { { "rte_ether_addr_hash", "hash" },
                                   { "VIGOR_DEVICE", "device" } };
  }

  void push() { frames.emplace_back(); }
  void pop() { frames.pop_back(); }

  void translate(std::string &var) const {
    auto it = cp_var_to_code_translation.find(var);
    if (it != cp_var_to_code_translation.end()) {
      var = it->second;
    }
  }

  void add(std::string &label) {
    assert(frames.size());
    translate(label);
    frames.back().emplace_back(label);
  }

  void add(std::string &label, klee::ref<klee::Expr> value) {
    assert(frames.size());
    translate(label);
    frames.back().emplace_back(label, value);
  }

  void add(std::string &label, klee::ref<klee::Expr> value,
           klee::ref<klee::Expr> addr) {
    assert(frames.size());
    translate(label);
    frames.back().emplace_back(label, value, addr);
  }

  bool has_label(std::string &label) {
    translate(label);

    for (auto it = frames.rbegin(); it != frames.rend(); it++) {
      for (auto var : *it) {
        if (var.label == label) {
          return true;
        }
      }
    }

    return false;
  }

  klee::ref<klee::Expr> get_value(std::string label) {
    klee::ref<klee::Expr> ret;
    translate(label);

    for (auto it = frames.rbegin(); it != frames.rend(); it++) {
      for (auto var : *it) {
        if (var.label == label) {
          return var.value;
        }
      }
    }

    return ret;
  }

  void set_value(std::string label, klee::ref<klee::Expr> value) {
    translate(label);

    for (auto it = frames.rbegin(); it != frames.rend(); it++) {
      for (auto &var : *it) {
        if (var.label == label) {
          var.value = value;
          return;
        }
      }
    }

    assert(false);
  }

  void set_addr(std::string label, klee::ref<klee::Expr> addr) {
    translate(label);

    for (auto it = frames.rbegin(); it != frames.rend(); it++) {
      for (auto &var : *it) {
        if (var.label == label) {
          var.addr = addr;
          return;
        }
      }
    }

    assert(false);
  }

  klee::ref<klee::Expr> get_value(klee::ref<klee::Expr> addr) {
    klee::ref<klee::Expr> ret;
    auto size = addr->getWidth();

    for (auto it = frames.rbegin(); it != frames.rend(); it++) {
      for (auto var : *it) {
        auto target = var.addr;

        if (var.addr.isNull()) {
          continue;
        }

        auto extracted =
            BDD::solver_toolbox.exprBuilder->Extract(target, 0, size);
        if (BDD::solver_toolbox.are_exprs_always_equal(extracted, addr)) {
          return var.value;
        }
      }
    }

    return ret;
  }

  std::string get_label(klee::ref<klee::Expr> addr) {
    std::string label;
    auto size = addr->getWidth();

    for (auto it = frames.rbegin(); it != frames.rend(); it++) {
      for (auto var : *it) {
        auto target = var.addr;

        if (var.addr.isNull()) {
          continue;
        }

        auto extracted =
            BDD::solver_toolbox.exprBuilder->Extract(target, 0, size);
        if (BDD::solver_toolbox.are_exprs_always_equal(extracted, addr)) {
          return var.label;
        }
      }
    }

    return label;
  }

  std::string get_by_value(klee::ref<klee::Expr> value) {
    std::stringstream label_stream;

    auto value_size = value->getWidth();

    for (auto it = frames.rbegin(); it != frames.rend(); it++) {
      for (auto var : *it) {
        if (var.value.isNull()) {
          continue;
        }

        RetrieveSymbols retriever;
        retriever.visit(var.value);
        auto symbols = retriever.get_retrieved_strings();

        if (symbols.size() == 0) {
          continue;
        }

        auto var_size = var.value->getWidth();

        if (var_size < value_size) {
          continue;
        }

        if (var_size == value_size &&
            BDD::solver_toolbox.are_exprs_always_equal(var.value, value)) {
          if (!var.addr.isNull() && (value_size == 8 || value_size == 16 || value_size == 32 || value_size == 64)) {
            assert(value_size % 8 == 0 && value_size <= 64);

            label_stream << "*(";

            label_stream << "(uint";
            label_stream << value_size;
            label_stream << "_t*)";

            label_stream << var.label;

            label_stream << ")";
          } else {
            label_stream << var.label;
          }

          return label_stream.str();
        }

        for (unsigned b = 0; b + value_size <= var_size; b += 8) {
          auto var_extract = BDD::solver_toolbox.exprBuilder->Extract(
              var.value, b, value_size);

          if (BDD::solver_toolbox.are_exprs_always_equal(var_extract, value)) {

            if (!var.addr.isNull() && value_size == 8) {
              label_stream << var.label << "[" << b / 8 << "]";
            } else if (!var.addr.isNull()) {
              assert(value_size % 8 == 0);

              if (value_size > 64) {
                return var.label;
              }

              label_stream << "*(";

              label_stream << "(uint";
              label_stream << value_size;
              label_stream << "_t*)";

              label_stream << "(" << var.label << "+" << b / 8 << ")";

              label_stream << ")";
            } else {

              // time should be 64 bits
              if (var.label == "now" && value_size == 32 && b == 0) {
                return var.label;
              }

              uint64_t mask = 0;
              for (unsigned bmask = 0; bmask < value_size; bmask++) {
                mask <<= 1;
                mask |= 1;
              }
              if (b > 0) {
                label_stream << "(";
              }
              label_stream << var.label;
              if (b > 0) {
                label_stream << " >> " << b << ")";
              }
              label_stream << " & " << mask;
            }

            return label_stream.str();
          }
        }
      }
    }

    return label_stream.str();
  }

  void not_found_err(klee::ref<klee::Expr> addr) const {
    Log::err() << "FAILED search for addr " << expr_to_string(addr, true)
               << "\n";
    Log::err() << "Dumping stack content...\n";
    err_dump();
    assert(false);
  }

  void err_dump() const {
    Log::err() << "============================================\n";
    for (auto it = frames.rbegin(); it != frames.rend(); it++) {
      Log::err() << "-------------------------------------------\n";
      for (auto var : *it) {
        std::stringstream ss;
        ss << var.label;
        if (!var.addr.isNull()) {
          ss << " : " << expr_to_string(var.addr, true);
        }

        if (!var.value.isNull()) {
          ss << " : " << expr_to_string(var.value, true);
        }
        Log::err() << ss.str() << "\n";
      }
    }
    Log::err() << "============================================\n";
  }
};

class x86_Generator : public TargetCodeGenerator {
private:
  struct p4_table {
    std::string name;
    std::string label;
    std::string tag;
    unsigned n_keys;
    unsigned n_params;
  };

private:
  std::stringstream global_state_stream;
  std::stringstream runtime_configure_stream;
  std::stringstream nf_init_stream;
  std::stringstream nf_process_stream;

  int lvl;
  std::stack<bool> pending_ifs;
  stack_t stack;
  std::vector<std::pair<klee::ref<klee::Expr>, uint64_t>> expiration_times;

  std::pair<bool, Target> is_controller;

private:
  void pad(std::ostream &_os) const { _os << std::string(lvl * 2, ' '); }

  int close_if_clauses();

  void fill_is_controller();

  void issue_write_to_switch(klee::ref<klee::Expr> libvig_obj,
                             klee::ref<klee::Expr> key,
                             klee::ref<klee::Expr> value);

  std::vector<p4_table>
  get_associated_p4_tables(klee::ref<klee::Expr> libvig_obj) const;

  std::pair<bool, uint64_t>
  get_expiration_time(klee::ref<klee::Expr> libvig_obj) const;

  void build_runtime_configure();
  void allocate(const ExecutionPlan &ep);
  void allocate_map(call_t call, std::ostream &global_state,
                    std::ostream &buffer);
  void allocate_vector(call_t call, std::ostream &global_state,
                       std::ostream &buffer);
  void allocate_dchain(call_t call, std::ostream &global_state,
                       std::ostream &buffer);
  void allocate_cht(call_t call, std::ostream &global_state,
                    std::ostream &buffer);

public:
  x86_Generator()
      : TargetCodeGenerator(GET_BOILERPLATE_PATH("boilerplate.c")), lvl(0),
        stack() {}

  void visit(ExecutionPlan ep) override;
  void visit(const ExecutionPlanNode *ep_node) override;

  void visit(const targets::x86::MapGet *node) override;
  void visit(const targets::x86::CurrentTime *node) override;
  void visit(const targets::x86::PacketBorrowNextChunk *node) override;
  void visit(const targets::x86::PacketGetMetadata *node) override;
  void visit(const targets::x86::PacketReturnChunk *node) override;
  void visit(const targets::x86::If *node) override;
  void visit(const targets::x86::Then *node) override;
  void visit(const targets::x86::Else *node) override;
  void visit(const targets::x86::Forward *node) override;
  void visit(const targets::x86::Broadcast *node) override;
  void visit(const targets::x86::Drop *node) override;
  void visit(const targets::x86::ExpireItemsSingleMap *node) override;
  void visit(const targets::x86::RteEtherAddrHash *node) override;
  void visit(const targets::x86::DchainRejuvenateIndex *node) override;
  void visit(const targets::x86::VectorBorrow *node) override;
  void visit(const targets::x86::VectorReturn *node) override;
  void visit(const targets::x86::DchainAllocateNewIndex *node) override;
  void visit(const targets::x86::MapPut *node) override;
  void visit(const targets::x86::PacketGetUnreadLength *node) override;
  void visit(const targets::x86::SetIpv4UdpTcpChecksum *node) override;
  void visit(const targets::x86::DchainIsIndexAllocated *node) override;
};
} // namespace synapse
