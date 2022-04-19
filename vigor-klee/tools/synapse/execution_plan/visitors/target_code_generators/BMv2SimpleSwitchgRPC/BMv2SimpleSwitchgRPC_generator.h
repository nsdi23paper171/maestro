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

class KleeExprToP4;
class KeysFromKleeExpr;

class BMv2SimpleSwitchgRPC_Generator : public TargetCodeGenerator {
  friend class KleeExprToP4;
  friend class KeysFromKleeExpr;

private:
  static void pad(std::ostream &_os, unsigned lvl) {
    _os << std::string(lvl * 2, ' ');
  }

  struct stage_t {
    std::string label;
    unsigned lvl;
    std::stack<bool> pending_ifs;

    stage_t(std::string _label) : label(_label), lvl(1) {}
    stage_t(std::string _label, unsigned _lvl) : label(_label), lvl(_lvl) {}

    virtual void dump(code_builder_t &code_builder) = 0;

    int close_if_clauses(std::ostream &os) {
      int closed = 0;

      if (!pending_ifs.size()) {
        return closed;
      }

      while (pending_ifs.size()) {
        lvl--;
        pad(os, lvl);
        os << "}\n";

        closed++;

        auto if_clause = pending_ifs.top();
        pending_ifs.pop();

        if (if_clause) {
          pending_ifs.push(false);
          break;
        }
      }

      return closed;
    }
  };

  struct header_field_t {
    uint64_t sz;
    std::string type;
    std::string label;
    klee::ref<klee::Expr> var_length;

    header_field_t(uint64_t _sz, const std::string &_label)
        : sz(_sz), label(_label) {
      std::stringstream ss;
      ss << "bit<" << sz << ">";
      type = ss.str();
    }

    header_field_t(uint64_t _sz, const std::string &_label,
                   klee::ref<klee::Expr> _var_length)
        : sz(_sz), label(_label), var_length(_var_length) {
      assert(!var_length.isNull());
      std::stringstream ss;
      ss << "varbit<" << sz << ">";
      type = ss.str();
    }
  };

  struct header_t {
    klee::ref<klee::Expr> chunk;
    std::string type_label;
    std::string label;
    std::vector<header_field_t> fields;

    header_t(const klee::ref<klee::Expr> &_chunk, const std::string &_label,
             const std::vector<header_field_t> &_fields)
        : chunk(_chunk), type_label(_label + "_t"), label(_label),
          fields(_fields) {
      assert(!chunk.isNull());

      bool size_check = true;

      unsigned total_sz = 0;
      for (auto field : fields) {
        total_sz += field.sz;
        size_check &= field.var_length.isNull();
      }

      assert(!size_check || total_sz <= chunk->getWidth());
    }
  };

  struct metadata_t {
    std::string label;
    std::vector<klee::ref<klee::Expr>> exprs;
    unsigned sz;

    metadata_t(std::string _label, std::vector<klee::ref<klee::Expr>> _exprs)
        : label(_label), exprs(_exprs) {
      assert(_exprs.size());
      assert(!_exprs[0].isNull());

      sz = _exprs[0]->getWidth();
      for (auto expr : exprs) {
        assert(expr->getWidth() == sz);
      }
    }

    metadata_t(std::string _label, unsigned _sz) : label(_label), sz(_sz) {}
  };

  struct metadata_stack_t {
    std::vector<metadata_t> all_metadata;
    std::vector<std::vector<metadata_t>> stack;

    metadata_stack_t() { push(); }

    void push() { stack.emplace_back(); }

    void pop() {
      assert(stack.size());
      stack.pop_back();
    }

    std::vector<metadata_t> get() const {
      std::vector<metadata_t> meta;

      for (auto frame : stack) {
        meta.insert(meta.end(), frame.begin(), frame.end());
      }

      return meta;
    }

    std::vector<metadata_t> get_all() const { return all_metadata; }

    void append(metadata_t meta) {
      assert(stack.size());

      auto found_it = std::find_if(
          all_metadata.begin(), all_metadata.end(),
          [&](const metadata_t &_meta) { return meta.label == _meta.label; });

      if (found_it != all_metadata.end()) {
        (*found_it) = meta;
      } else {
        all_metadata.push_back(meta);
      }

      stack.back().push_back(meta);
    }
  };

  struct var_t {
    std::string label;
    std::string symbol;
    uint64_t size;

    var_t(std::string _label, std::string _symbol, uint64_t _size)
        : label(_label), symbol(_symbol), size(_size) {}

    var_t(std::string _label, uint64_t _size)
        : label(_label), symbol(""), size(_size) {}
  };

  struct var_stack_t {
    std::vector<std::vector<var_t>> stack;

    var_stack_t() { push(); }

    void push() { stack.emplace_back(); }

    void pop() {
      assert(stack.size());
      stack.pop_back();
    }

    std::vector<var_t> get() const {
      std::vector<var_t> vars;

      for (auto frame : stack) {
        vars.insert(vars.end(), frame.begin(), frame.end());
      }

      return vars;
    }

    void append(var_t var) {
      assert(stack.size());
      stack.back().push_back(var);
    }
  };

  struct table_t {
    std::string label;
    std::vector<std::string> keys;
    std::vector<std::string> params_type;
    std::vector<metadata_t> meta_params;

    uint64_t size;

    table_t(std::string _label, std::vector<std::string> _keys,
            std::vector<std::string> _params_type,
            std::vector<metadata_t> _meta_params)
        : label(_label), keys(_keys), params_type(_params_type),
          meta_params(_meta_params), size(1024) {}

    void dump(std::ostream &os, unsigned lvl) {
      // ============== Action ==============
      os << "\n";

      pad(os, lvl);
      os << "action " << label << "_populate(";
      for (auto i = 0u; i < params_type.size(); i++) {
        std::stringstream param_label;
        param_label << "param_" << i;
        if (i != 0) {
          os << ", ";
        }
        os << params_type[i] << " " << param_label.str();
      }
      os << ") {\n";

      for (auto i = 0u; i < params_type.size(); i++) {
        std::stringstream param_label;
        param_label << "param_" << i;

        pad(os, lvl + 1);
        os << "meta." << meta_params[i].label << " = " << param_label.str()
           << ";\n";
      }

      pad(os, lvl);
      os << "}\n";

      // ============== Table ==============
      os << "\n";

      pad(os, lvl);
      os << "table " << label << " {\n";
      lvl++;

      pad(os, lvl);
      os << "key = {\n";

      lvl++;

      pad(os, lvl);
      os << "meta." << label << "_tag: range;\n";

      for (auto key : keys) {
        pad(os, lvl);
        os << key << ": exact;\n";
      }

      lvl--;
      pad(os, lvl);
      os << "}\n";

      os << "\n";

      pad(os, lvl);
      os << "actions = {\n";

      pad(os, lvl + 1);
      os << label << "_populate"
         << ";\n";

      pad(os, lvl);
      os << "}\n";

      os << "\n";

      pad(os, lvl);
      os << "size = " << size << ";\n";

      pad(os, lvl);
      os << "support_timeout = true;\n";

      lvl--;
      pad(os, lvl);
      os << "}\n";
    }
  };

  struct parser_t : stage_t {
    enum stage_type {
      CONDITIONAL,
      EXTRACTOR,
      TERMINATOR
    };

    struct parsing_stage {
      stage_type type;
      std::string label;

      parsing_stage(stage_type _type, const std::string &_label)
          : type(_type), label(_label) {}
    };

    struct conditional_stage : parsing_stage {
      std::string condition;
      bool transition_value;

      std::shared_ptr<parsing_stage> next_on_true;
      std::shared_ptr<parsing_stage> next_on_false;

      conditional_stage(const std::string &_label,
                        const std::string &_condition)
          : parsing_stage(CONDITIONAL, _label), condition(_condition) {}
    };

    struct extractor_stage : parsing_stage {
      std::string hdr;
      std::string dynamic_length;
      std::shared_ptr<parsing_stage> next;

      extractor_stage(const std::string &_label, const std::string &_hdr,
                      const std::string &_dynamic_length)
          : parsing_stage(EXTRACTOR, _label), hdr(_hdr),
            dynamic_length(_dynamic_length) {}
    };

    std::shared_ptr<parsing_stage> accept_stage;
    std::shared_ptr<parsing_stage> reject_stage;

    std::stack<std::shared_ptr<parsing_stage>> stages_stack;
    std::vector<std::shared_ptr<parsing_stage>> stages;
    int stage_id = 0;

    parser_t()
        : stage_t("SyNAPSE_Parser"),
          accept_stage(std::shared_ptr<parsing_stage>(
              new parsing_stage(TERMINATOR, "accept"))),
          reject_stage(std::shared_ptr<parsing_stage>(
              new parsing_stage(TERMINATOR, "reject"))) {}

    void dump(code_builder_t &code_builder) override;

    void transition(std::shared_ptr<parsing_stage> new_stage) {
      assert(stages_stack.size());
      auto current = stages_stack.top();

      if (current->type == CONDITIONAL) {
        auto conditional = static_cast<conditional_stage *>(current.get());

        if (conditional->transition_value && !conditional->next_on_true) {
          conditional->next_on_true = new_stage;
        } else if (!conditional->next_on_false) {
          conditional->next_on_false = new_stage;
        }
      } else if (current->type == EXTRACTOR) {
        auto extractor = static_cast<extractor_stage *>(current.get());

        if (!extractor->next) {
          extractor->next = new_stage;
        }
      } else {
        assert(false &&
               "Should not transition from an already terminated stage");
      }
    }

    void accept() { transition(accept_stage); }
    void reject() { transition(reject_stage); }

    void push_on_true() {
      assert(stages_stack.size());
      auto current = stages_stack.top();

      assert(current->type == CONDITIONAL);
      auto conditional = static_cast<conditional_stage *>(current.get());
      conditional->transition_value = true;
    }

    void push_on_false() {
      assert(stages_stack.size());
      auto current = stages_stack.top();

      assert(current->type == CONDITIONAL);
      auto conditional = static_cast<conditional_stage *>(current.get());
      conditional->transition_value = false;
    }

    void pop() {
      assert(stages_stack.size());

      if (stages_stack.top()->type == CONDITIONAL) {
        auto conditional =
            static_cast<conditional_stage *>(stages_stack.top().get());
        if (conditional->transition_value) {
          return;
        }
      }

      stages_stack.pop();
    }

    void add_condition(const std::string &condition) {
      std::stringstream conditional_label;

      conditional_label << "conditional_stage_";
      conditional_label << stage_id;

      stage_id++;

      auto new_stage = std::shared_ptr<parsing_stage>(
          new conditional_stage(conditional_label.str(), condition));

      transition(new_stage);

      stages.push_back(new_stage);
      stages_stack.push(new_stage);
    }

    void add_extractor(std::string label) {
      add_extractor(label, std::string());
    }

    void add_extractor(std::string hdr, std::string dynamic_length) {
      std::stringstream label_stream;
      label_stream << "parse_";
      label_stream << hdr;
      label_stream << "_";
      label_stream << stage_id;

      stage_id++;

      auto new_stage = std::shared_ptr<parsing_stage>(
          new extractor_stage(label_stream.str(), hdr, dynamic_length));

      if (stages_stack.size()) {
        transition(new_stage);
      }

      stages.push_back(new_stage);
      stages_stack.push(new_stage);
    }

    void print() {
      if (!stages.size()) {
        return;
      }

      std::cerr << "====== STAGES ======\n";
      std::cerr << "current " << stages_stack.top()->label << "\n";
      print(stages[0]);
      std::cerr << "====================\n\n";
    }

    void print(std::shared_ptr<parsing_stage> stage) {
      if (!stage) {
        return;
      }

      if (stage->type == CONDITIONAL) {
        std::cerr << "stage " << stage->label;
        std::cerr << "\n";

        auto conditional = static_cast<conditional_stage *>(stage.get());

        std::cerr << "   true:   ";
        if (conditional->next_on_true) {
          std::cerr << conditional->next_on_true->label;
        }
        std::cerr << "\n";

        std::cerr << "   false:  ";
        if (conditional->next_on_false) {
          std::cerr << conditional->next_on_false->label;
        }
        std::cerr << "\n";

        print(conditional->next_on_true);
        print(conditional->next_on_false);
      } else if (stage->type == EXTRACTOR) {
        std::cerr << "stage " << stage->label;
        std::cerr << "\n";

        auto extractor = static_cast<extractor_stage *>(stage.get());

        std::cerr << "   next:   ";
        if (extractor->next) {
          std::cerr << extractor->next->label;
        }
        std::cerr << "\n";

        print(extractor->next);
      }
    }
  };

  struct verify_checksum_t : stage_t {
    verify_checksum_t() : stage_t("SyNAPSE_VerifyChecksum") {}

    void dump(code_builder_t &code_builder) override;
  };

  struct ingress_t : stage_t {
    std::stringstream apply_block;

    std::vector<table_t> tables;
    std::vector<var_t> key_bytes;

    ingress_t() : stage_t("SyNAPSE_Ingress", 2) {}

    void dump(code_builder_t &code_builder) override;
  };

  struct egress_t : stage_t {
    std::stack<bool> pending_ifs;
    egress_t() : stage_t("SyNAPSE_Egress") {}

    void dump(code_builder_t &code_builder) override;
  };

  struct compute_checksum_t : stage_t {
    compute_checksum_t() : stage_t("SyNAPSE_ComputeChecksum") {}

    void dump(code_builder_t &code_builder) override;
  };

  struct deparser_t : stage_t {
    std::vector<std::string> headers_labels;
    deparser_t() : stage_t("SyNAPSE_Deparser") {}

    void dump(code_builder_t &code_builder) override;
  };

private:
  bool parsing_headers;

  std::vector<header_t> headers;
  metadata_stack_t metadata;
  var_stack_t local_vars;

  // pipeline stages
  parser_t parser;
  verify_checksum_t verify_checksum;
  ingress_t ingress;
  egress_t egress;
  compute_checksum_t compute_checksum;
  deparser_t deparser;

private:
  void dump();

  bool is_constant(klee::ref<klee::Expr> expr) const;
  bool is_constant_signed(klee::ref<klee::Expr> expr) const;
  int64_t get_constant_signed(klee::ref<klee::Expr> expr) const;

  std::string p4_type_from_expr(klee::ref<klee::Expr> expr) const;
  void field_header_from_packet_chunk(klee::ref<klee::Expr> expr,
                                      std::string &field,
                                      unsigned &bit_offset) const;
  std::string label_from_packet_chunk(klee::ref<klee::Expr> expr) const;
  std::string label_from_vars(klee::ref<klee::Expr> expr) const;
  std::vector<std::string> assign_key_bytes(klee::ref<klee::Expr> expr);
  std::string transpile(const klee::ref<klee::Expr> &e,
                        bool is_signed = false) const;
  void err_label_from_chunk(klee::ref<klee::Expr> expr) const;
  void err_label_from_vars(klee::ref<klee::Expr> expr) const;

public:
  BMv2SimpleSwitchgRPC_Generator()
      : TargetCodeGenerator(GET_BOILERPLATE_PATH("boilerplate.p4")),
        parsing_headers(true) {}

  void visit(ExecutionPlan ep) override;
  void visit(const ExecutionPlanNode *ep_node) override;

  void visit(const targets::BMv2SimpleSwitchgRPC::Drop *node) override;
  void visit(const targets::BMv2SimpleSwitchgRPC::Else *node) override;
  void
  visit(const targets::BMv2SimpleSwitchgRPC::EthernetConsume *node) override;
  void
  visit(const targets::BMv2SimpleSwitchgRPC::EthernetModify *node) override;
  void visit(const targets::BMv2SimpleSwitchgRPC::Forward *node) override;
  void visit(const targets::BMv2SimpleSwitchgRPC::If *node) override;
  void visit(const targets::BMv2SimpleSwitchgRPC::Ignore *node) override;
  void visit(const targets::BMv2SimpleSwitchgRPC::IPv4Consume *node) override;
  void visit(const targets::BMv2SimpleSwitchgRPC::IPv4Modify *node) override;
  void
  visit(const targets::BMv2SimpleSwitchgRPC::IPOptionsConsume *node) override;
  void
  visit(const targets::BMv2SimpleSwitchgRPC::IPOptionsModify *node) override;
  void visit(const targets::BMv2SimpleSwitchgRPC::TcpUdpConsume *node) override;
  void visit(const targets::BMv2SimpleSwitchgRPC::TcpUdpModify *node) override;
  void
  visit(const targets::BMv2SimpleSwitchgRPC::SendToController *node) override;
  void
  visit(const targets::BMv2SimpleSwitchgRPC::SetupExpirationNotifications *node)
      override;
  void visit(const targets::BMv2SimpleSwitchgRPC::TableLookup *node) override;
  void visit(const targets::BMv2SimpleSwitchgRPC::Then *node) override;
};
} // namespace synapse
