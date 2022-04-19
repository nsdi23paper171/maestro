#pragma once

#include <unordered_set>
#include <vector>

#include "load-call-paths.h"
#include "printer.h"
#include "solver_toolbox.h"

#include "./visitor.h"

namespace BDD {

struct symbol_t {
  std::string label;
  std::string label_base;
  klee::ref<klee::Expr> expr;
  klee::ref<klee::Expr> addr;

  symbol_t(std::string _label, std::string _label_base,
           klee::ref<klee::Expr> _expr)
      : label(_label), label_base(_label_base), expr(_expr) {}

  symbol_t(std::string _label, std::string _label_base,
           klee::ref<klee::Expr> _expr, klee::ref<klee::Expr> _addr)
      : label(_label), label_base(_label_base), expr(_expr), addr(_addr) {}
};

inline bool operator==(const symbol_t &lhs, const symbol_t &rhs) {
  if (lhs.label != rhs.label) {
    return false;
  }

  if (lhs.label_base != rhs.label_base) {
    return false;
  }

  if (!solver_toolbox.are_exprs_always_equal(lhs.expr, rhs.expr)) {
    return false;
  }

  if (!solver_toolbox.are_exprs_always_equal(lhs.addr, rhs.addr)) {
    return false;
  }

  return true;
}

struct symbol_t_hash {
  std::size_t operator()(const symbol_t &_node) const {
    return std::hash<std::string>()(_node.label);
  }
};

typedef std::unordered_set<symbol_t, symbol_t_hash> symbols_t;
typedef std::pair<call_path_t *, calls_t> call_path_pair_t;

struct call_paths_t {
  std::vector<call_path_t *> cp;
  std::vector<calls_t> backup;

  static std::vector<std::string> skip_functions;

  call_paths_t() {}

  call_paths_t(const std::vector<call_path_t *> &_call_paths)
      : cp(_call_paths) {
    for (const auto &_cp : cp) {
      backup.emplace_back(_cp->calls);
    }

    assert(cp.size() == backup.size());
  }

  size_t size() const { return cp.size(); }

  call_path_pair_t get(unsigned int i) {
    assert(i < size());
    return call_path_pair_t(cp[i], backup[i]);
  }

  void clear() {
    cp.clear();
    backup.clear();
  }

  void push_back(call_path_pair_t pair) {
    cp.push_back(pair.first);
    backup.push_back(pair.second);
  }

  static bool is_skip_function(const std::string &fname);
};

class Call;

class CallPathsGroup {
private:
  klee::ref<klee::Expr> constraint;
  call_paths_t on_true;
  call_paths_t on_false;

  call_paths_t call_paths;

private:
  void group_call_paths();
  bool check_discriminating_constraint(klee::ref<klee::Expr> constraint);
  klee::ref<klee::Expr> find_discriminating_constraint();
  std::vector<klee::ref<klee::Expr>>
  get_possible_discriminating_constraints() const;
  bool satisfies_constraint(std::vector<call_path_t *> call_paths,
                            klee::ref<klee::Expr> constraint) const;
  bool satisfies_constraint(call_path_t *call_path,
                            klee::ref<klee::Expr> constraint) const;
  bool satisfies_not_constraint(std::vector<call_path_t *> call_paths,
                                klee::ref<klee::Expr> constraint) const;
  bool satisfies_not_constraint(call_path_t *call_path,
                                klee::ref<klee::Expr> constraint) const;
  bool are_calls_equal(call_t c1, call_t c2);
  call_t pop_call();

public:
  CallPathsGroup(const call_paths_t &_call_paths) : call_paths(_call_paths) {
    group_call_paths();
  }

  klee::ref<klee::Expr> get_discriminating_constraint() const {
    return constraint;
  }

  call_paths_t get_on_true() const { return on_true; }
  call_paths_t get_on_false() const { return on_false; }
};

class BDDVisitor;
class Node;

typedef std::shared_ptr<Node> BDDNode_ptr;

class Node {
  friend class SymbolFactory;

public:
  enum NodeType {
    BRANCH,
    CALL,
    RETURN_INIT,
    RETURN_PROCESS,
    RETURN_RAW
  };

protected:
  friend class Call;
  friend class Branch;
  friend class ReturnRaw;
  friend class ReturnInit;
  friend class ReturnProcess;

  uint64_t id;
  NodeType type;

  BDDNode_ptr next;
  BDDNode_ptr prev;

  std::vector<std::string> call_paths_filenames;
  std::vector<klee::ConstraintManager> constraints;

  virtual std::string get_gv_name() const {
    std::stringstream ss;
    ss << id;
    return ss.str();
  }

public:
  Node(uint64_t _id, NodeType _type)
      : id(_id), type(_type), next(nullptr), prev(nullptr) {}

  Node(uint64_t _id, NodeType _type,
       const std::vector<call_path_t *> &_call_paths)
      : id(_id), type(_type), next(nullptr), prev(nullptr) {
    process_call_paths(_call_paths);
  }

  Node(uint64_t _id, NodeType _type, const BDDNode_ptr &_next,
       const BDDNode_ptr &_prev, const std::vector<call_path_t *> &_call_paths)
      : id(_id), type(_type), next(_next), prev(_prev) {
    process_call_paths(_call_paths);
  }

  Node(uint64_t _id, NodeType _type, const BDDNode_ptr &_next,
       const BDDNode_ptr &_prev,
       const std::vector<std::string> &_call_paths_filenames,
       const std::vector<klee::ConstraintManager> &_constraints)
      : id(_id), type(_type), next(_next), prev(_prev),
        call_paths_filenames(_call_paths_filenames), constraints(_constraints) {
  }

  void replace_next(const BDDNode_ptr &_next) { next = _next; }

  void add_next(const BDDNode_ptr &_next) {
    assert(next == nullptr);
    assert(_next);

    next = _next;
  }

  void replace_prev(const BDDNode_ptr &_prev) { prev = _prev; }

  void add_prev(const BDDNode_ptr &_prev) {
    assert(prev == nullptr);
    assert(_prev);
    prev = _prev;
  }

  const BDDNode_ptr &get_next() const { return next; }
  const BDDNode_ptr &get_next() { return next; }

  const BDDNode_ptr &get_prev() const { return prev; }
  const BDDNode_ptr &get_prev() { return prev; }

  NodeType get_type() const { return type; }
  uint64_t get_id() const { return id; }

  const std::vector<std::string> &get_call_paths_filenames() const {
    return call_paths_filenames;
  }

  const std::vector<klee::ConstraintManager> &get_constraints() const {
    return constraints;
  }

  void
  set_constraints(const std::vector<klee::ConstraintManager> &_constraints) {
    constraints = _constraints;
  }

  symbols_t get_all_generated_symbols() const;

  virtual BDDNode_ptr clone(bool recursive = false) const = 0;

  virtual void recursive_update_ids(uint64_t &new_id) = 0;
  void update_id(uint64_t new_id);

  static std::string
  process_call_path_filename(std::string call_path_filename) {
    std::string dir_delim = "/";
    std::string ext_delim = ".";

    auto dir_found = call_path_filename.find_last_of(dir_delim);
    if (dir_found != std::string::npos) {
      call_path_filename =
          call_path_filename.substr(dir_found + 1, call_path_filename.size());
    }

    auto ext_found = call_path_filename.find_last_of(ext_delim);
    if (ext_found != std::string::npos) {
      call_path_filename = call_path_filename.substr(0, ext_found);
    }

    return call_path_filename;
  }

  void process_call_paths(std::vector<call_path_t *> call_paths) {
    std::string dir_delim = "/";
    std::string ext_delim = ".";

    for (const auto &cp : call_paths) {
      constraints.push_back(cp->constraints);
      auto filename = process_call_path_filename(cp->file_name);
      call_paths_filenames.push_back(filename);
    }
  }

  virtual void visit(BDDVisitor &visitor) const = 0;
  virtual std::string dump(bool one_liner = false) const = 0;

  virtual std::string dump_recursive(int lvl = 0) const {
    std::stringstream result;

    auto pad = std::string(lvl * 2, ' ');

    result << pad << dump(true) << "\n";

    if (next) {
      result << next->dump_recursive(lvl + 1);
    }

    return result.str();
  }
};

class Call : public Node {
private:
  call_t call;

public:
  Call(uint64_t _id, call_t _call,
       const std::vector<call_path_t *> &_call_paths)
      : Node(_id, Node::NodeType::CALL, _call_paths), call(_call) {}

  Call(uint64_t _id, call_t _call, const BDDNode_ptr &_next,
       const BDDNode_ptr &_prev, const std::vector<call_path_t *> &_call_paths)
      : Node(_id, Node::NodeType::CALL, _next, _prev, _call_paths),
        call(_call) {}

  Call(uint64_t _id, call_t _call, const BDDNode_ptr &_next,
       const BDDNode_ptr &_prev,
       const std::vector<std::string> &_call_paths_filenames,
       const std::vector<klee::ConstraintManager> &_constraints)
      : Node(_id, Node::NodeType::CALL, _next, _prev, _call_paths_filenames,
             _constraints),
        call(_call) {}

  call_t get_call() const { return call; }
  void set_call(call_t _call) { call = _call; }

  symbols_t get_generated_symbols() const;

  virtual BDDNode_ptr clone(bool recursive = false) const override {
    BDDNode_ptr clone_next;

    if (recursive && next) {
      clone_next = next->clone(true);
    } else {
      clone_next = next;
    }

    auto clone = std::make_shared<Call>(id, call, clone_next, prev,
                                        call_paths_filenames, constraints);

    if (recursive && clone_next) {
      clone_next->prev = clone;
    }

    return clone;
  }

  virtual void recursive_update_ids(uint64_t &new_id) override;

  void visit(BDDVisitor &visitor) const override { visitor.visit(this); }

  std::string dump(bool one_liner = false) const override {
    std::stringstream ss;
    ss << id << ":";
    ss << call.function_name;
    ss << "(";

    bool first = true;
    for (auto &arg : call.args) {
      if (!first)
        ss << ", ";
      ss << arg.first << ":";
      ss << expr_to_string(arg.second.expr, one_liner);
      if (!arg.second.in.isNull() || !arg.second.out.isNull()) {
        ss << "[";
      }
      if (!arg.second.in.isNull()) {
        ss << expr_to_string(arg.second.in, one_liner);
      }
      if (!arg.second.out.isNull()) {
        ss << " -> ";
        ss << expr_to_string(arg.second.out, one_liner);
      }
      if (!arg.second.in.isNull() || !arg.second.out.isNull()) {
        ss << "]";
      }
      first = false;
    }
    ss << ")";
    return ss.str();
  }
};

class Branch : public Node {
private:
  klee::ref<klee::Expr> condition;
  BDDNode_ptr on_false;

public:
  Branch(uint64_t _id, klee::ref<klee::Expr> _condition,
         const std::vector<call_path_t *> &_call_paths)
      : Node(_id, Node::NodeType::BRANCH, _call_paths), condition(_condition),
        on_false(nullptr) {}

  Branch(uint64_t _id, klee::ref<klee::Expr> _condition,
         const BDDNode_ptr &_on_true, const BDDNode_ptr &_on_false,
         const BDDNode_ptr &_prev,
         const std::vector<call_path_t *> &_call_paths)
      : Node(_id, Node::NodeType::BRANCH, _on_true, _prev, _call_paths),
        condition(_condition), on_false(_on_false) {}

  Branch(uint64_t _id, klee::ref<klee::Expr> _condition,
         const BDDNode_ptr &_on_true, const BDDNode_ptr &_on_false,
         const BDDNode_ptr &_prev,
         const std::vector<std::string> &_call_paths_filenames,
         const std::vector<klee::ConstraintManager> &_constraints)
      : Node(_id, Node::NodeType::BRANCH, _on_true, _prev,
             _call_paths_filenames, _constraints),
        condition(_condition), on_false(_on_false) {}

  klee::ref<klee::Expr> get_condition() const { return condition; }
  void set_condition(const klee::ref<klee::Expr> &_condition) {
    condition = _condition;
  }

  void replace_on_true(const BDDNode_ptr &_on_true) { replace_next(_on_true); }

  void add_on_true(const BDDNode_ptr &_on_true) { add_next(_on_true); }

  const BDDNode_ptr &get_on_true() const { return next; }
  BDDNode_ptr get_on_true() { return next; }

  void replace_on_false(const BDDNode_ptr &_on_false) { on_false = _on_false; }

  void add_on_false(const BDDNode_ptr &_on_false) { on_false = _on_false; }

  const BDDNode_ptr &get_on_false() const { return on_false; }
  BDDNode_ptr get_on_false() { return on_false; }

  virtual BDDNode_ptr clone(bool recursive = false) const override {
    BDDNode_ptr clone_on_true, clone_on_false;

    assert(next);
    assert(on_false);

    if (recursive) {
      clone_on_true = next->clone(true);
      clone_on_false = on_false->clone(true);
    } else {
      clone_on_true = next;
      clone_on_false = on_false;
    }

    auto clone =
        std::make_shared<Branch>(id, condition, clone_on_true, clone_on_false,
                                 prev, call_paths_filenames, constraints);

    if (recursive) {
      clone_on_true->prev = clone;
      clone_on_false->prev = clone;
    }

    return clone;
  }

  virtual void recursive_update_ids(uint64_t &new_id) override;

  void visit(BDDVisitor &visitor) const override { visitor.visit(this); }

  std::string dump(bool one_liner = false) const override {
    std::stringstream ss;
    ss << id << ":";
    ss << "if (";
    ss << expr_to_string(condition, one_liner);
    ss << ")";
    return ss.str();
  }

  std::string dump_recursive(int lvl = 0) const override {
    std::stringstream result;

    result << Node::dump_recursive(lvl);
    result << on_false->dump_recursive(lvl + 1);

    return result.str();
  }
};

class ReturnRaw : public Node {
private:
  std::vector<calls_t> calls_list;

public:
  ReturnRaw(uint64_t _id, call_paths_t call_paths)
      : Node(_id, Node::NodeType::RETURN_RAW, nullptr, nullptr, call_paths.cp),
        calls_list(call_paths.backup) {}

  ReturnRaw(uint64_t _id, const BDDNode_ptr &_prev,
            std::vector<calls_t> _calls_list,
            const std::vector<std::string> &_call_paths_filenames,
            const std::vector<klee::ConstraintManager> &_constraints)
      : Node(_id, Node::NodeType::RETURN_RAW, nullptr, _prev,
             _call_paths_filenames, _constraints),
        calls_list(_calls_list) {}

  virtual BDDNode_ptr clone(bool recursive = false) const override {
    auto clone = std::make_shared<ReturnRaw>(id, prev, calls_list,
                                             call_paths_filenames, constraints);
    return clone;
  }

  virtual void recursive_update_ids(uint64_t &new_id) override;

  std::vector<calls_t> get_calls() const { return calls_list; }

  void visit(BDDVisitor &visitor) const override { visitor.visit(this); }

  std::string dump(bool one_liner = false) const {
    std::stringstream ss;
    ss << id << ":";
    ss << "return raw";
    return ss.str();
  }
};

class ReturnInit : public Node {
public:
  enum ReturnType {
    SUCCESS,
    FAILURE
  };

private:
  ReturnType value;

  void fill_return_value(calls_t calls) {
    assert(calls.size());

    auto start_time_finder = [](call_t call)->bool {
      return call.function_name == "start_time";
    };

    auto start_time_it =
        std::find_if(calls.begin(), calls.end(), start_time_finder);
    auto found = (start_time_it != calls.end());

    value = found ? SUCCESS : FAILURE;
  }

public:
  ReturnInit(uint64_t _id)
      : Node(_id, Node::NodeType::RETURN_INIT), value(SUCCESS) {}

  ReturnInit(uint64_t _id, const ReturnRaw *raw)
      : Node(_id, Node::NodeType::RETURN_INIT, nullptr, nullptr,
             raw->call_paths_filenames, raw->constraints) {
    auto calls_list = raw->get_calls();
    assert(calls_list.size());
    fill_return_value(calls_list[0]);
  }

  ReturnInit(uint64_t _id, const BDDNode_ptr &_prev, ReturnType _value)
      : Node(_id, Node::NodeType::RETURN_INIT, nullptr, _prev,
             _prev->call_paths_filenames, _prev->constraints),
        value(_value) {}

  ReturnInit(uint64_t _id, const BDDNode_ptr &_prev, ReturnType _value,
             const std::vector<std::string> &_call_paths_filenames,
             std::vector<klee::ConstraintManager> _constraints)
      : Node(_id, Node::NodeType::RETURN_INIT, nullptr, _prev,
             _call_paths_filenames, _constraints),
        value(_value) {}

  ReturnType get_return_value() const { return value; }

  virtual BDDNode_ptr clone(bool recursive = false) const override {
    auto clone = std::make_shared<ReturnInit>(id, prev, value);
    return clone;
  }

  virtual void recursive_update_ids(uint64_t &new_id) override;

  void visit(BDDVisitor &visitor) const override { visitor.visit(this); }

  std::string dump(bool one_liner = false) const {
    std::stringstream ss;
    ss << id << ":";
    ss << "return ";

    switch (value) {
    case ReturnType::SUCCESS:
      ss << "SUCCESS";
      break;
    case ReturnType::FAILURE:
      ss << "FAILURE";
      break;
    }

    return ss.str();
  }
};

class ReturnProcess : public Node {
public:
  enum Operation {
    FWD,
    DROP,
    BCAST,
    ERR
  };

private:
  int value;
  Operation operation;

  std::pair<unsigned, unsigned> analyse_packet_sends(calls_t calls) const {
    unsigned counter = 0;
    unsigned dst_device = 0;

    for (const auto &call : calls) {
      if (call.function_name != "packet_send")
        continue;

      counter++;

      if (counter == 1) {
        auto dst_device_expr = call.args.at("dst_device").expr;
        assert(dst_device_expr->getKind() == klee::Expr::Kind::Constant);

        klee::ConstantExpr *dst_device_const =
            static_cast<klee::ConstantExpr *>(dst_device_expr.get());
        dst_device = dst_device_const->getZExtValue();
      }
    }

    return std::pair<unsigned, unsigned>(counter, dst_device);
  }

  void fill_return_value(calls_t calls) {
    auto counter_dst_device_pair = analyse_packet_sends(calls);

    if (counter_dst_device_pair.first == 1) {
      value = counter_dst_device_pair.second;
      operation = FWD;
      return;
    }

    if (counter_dst_device_pair.first > 1) {
      value = ((uint16_t) - 1);
      operation = BCAST;
      return;
    }

    auto packet_receive_finder = [](call_t call)->bool {
      return call.function_name == "packet_receive";
    };

    auto packet_receive_it =
        std::find_if(calls.begin(), calls.end(), packet_receive_finder);

    if (packet_receive_it == calls.end()) {
      operation = ERR;
      value = -1;
      return;
    }

    auto packet_receive = *packet_receive_it;
    auto src_device_expr = packet_receive.args["src_devices"].expr;
    assert(src_device_expr->getKind() == klee::Expr::Kind::Constant);

    klee::ConstantExpr *src_device_const =
        static_cast<klee::ConstantExpr *>(src_device_expr.get());
    auto src_device = src_device_const->getZExtValue();

    operation = DROP;
    value = src_device;
    return;
  }

public:
  ReturnProcess(uint64_t _id, const ReturnRaw *raw)
      : Node(_id, Node::NodeType::RETURN_PROCESS, nullptr, nullptr,
             raw->call_paths_filenames, raw->constraints) {
    auto calls_list = raw->get_calls();
    assert(calls_list.size());
    fill_return_value(calls_list[0]);
  }

  ReturnProcess(uint64_t _id, const BDDNode_ptr &_prev, int _value,
                Operation _operation)
      : Node(_id, Node::NodeType::RETURN_PROCESS, nullptr, _prev,
             _prev->call_paths_filenames, _prev->constraints),
        value(_value), operation(_operation) {}

  ReturnProcess(uint64_t _id, const BDDNode_ptr &_prev, int _value,
                Operation _operation,
                const std::vector<std::string> &_call_paths_filenames,
                std::vector<klee::ConstraintManager> _constraints)
      : Node(_id, Node::NodeType::RETURN_PROCESS, nullptr, _prev,
             _call_paths_filenames, _constraints),
        value(_value), operation(_operation) {}

  int get_return_value() const { return value; }

  Operation get_return_operation() const { return operation; }

  virtual BDDNode_ptr clone(bool recursive = false) const override {
    auto clone = std::make_shared<ReturnProcess>(id, prev, value, operation);
    return clone;
  }

  virtual void recursive_update_ids(uint64_t &new_id) override;

  void visit(BDDVisitor &visitor) const override { visitor.visit(this); }

  std::string dump(bool one_liner = false) const {
    std::stringstream ss;
    ss << id << ":";

    switch (operation) {
    case Operation::FWD:
      ss << "FORWARD";
      break;
    case Operation::DROP:
      ss << "DROP";
      break;
    case Operation::BCAST:
      ss << "BROADCAST";
      break;
    case Operation::ERR:
      ss << "ERR";
      break;
    }

    return ss.str();
  }
};
} // namespace BDD
