#pragma once

#include "./bdd-nodes.h"
#include "symbol-factory.h"

namespace BDD {

class BDD {
public:
  friend class CallPathsGroup;
  friend class Call;

private:
  uint64_t id;

  BDDNode_ptr nf_init;
  BDDNode_ptr nf_process;

  std::vector<call_path_t *> call_paths;

  static std::vector<std::string> skip_conditions_with_symbol;
  static constexpr char INIT_CONTEXT_MARKER[] = "start_time";

  // For deserialization
  BDD() : id(0) { solver_toolbox.build(); }

private:
  call_t get_successful_call(std::vector<call_path_t *> call_paths) const;
  BDDNode_ptr populate(call_paths_t call_paths);

  static std::string get_fname(const Node *node);
  static bool is_skip_function(const Node *node);
  static bool is_skip_condition(const Node *node);

  BDDNode_ptr populate_init(const BDDNode_ptr &root);
  BDDNode_ptr populate_process(const BDDNode_ptr &root, bool store = false);

  void add_node(call_t call);
  void dump(int lvl, BDDNode_ptr node) const;

  unsigned get_number_of_nodes(BDDNode_ptr root) const {
    unsigned num_nodes = 0;

    std::vector<BDDNode_ptr> nodes{ root };
    BDDNode_ptr node;

    while (nodes.size()) {
      node = nodes[0];
      num_nodes++;
      nodes.erase(nodes.begin());

      if (node->get_type() == Node::NodeType::BRANCH) {
        auto branch_node = static_cast<Branch *>(node.get());

        nodes.push_back(branch_node->get_on_true());
        nodes.push_back(branch_node->get_on_false());
      } else if (node->get_next()) {
        nodes.push_back(node->get_next());
      }
    }

    return num_nodes;
  }

public:
  BDD(std::vector<call_path_t *> _call_paths) : id(0), call_paths(_call_paths) {
    solver_toolbox.build();

    call_paths_t cp(call_paths);
    auto root = populate(cp);

    nf_init = populate_init(root);
    nf_process = populate_process(root);

    rename_symbols();
    trim_constraints();
  }

  BDD(const BDD &bdd)
      : id(bdd.id), nf_init(bdd.nf_init), nf_process(bdd.nf_process),
        call_paths(bdd.call_paths) {}

  BDD &operator=(const BDD &) = default;

  const BDDNode_ptr &get_init() const { return nf_init; }
  BDDNode_ptr get_init() { return nf_init; }

  const BDDNode_ptr &get_process() const { return nf_process; }
  BDDNode_ptr get_process() { return nf_process; }

  unsigned get_number_of_init_nodes() const {
    return get_number_of_nodes(nf_init);
  }

  unsigned get_number_of_process_nodes() const {
    return get_number_of_nodes(nf_process);
  }

  void replace_process(const BDDNode_ptr &_process) { nf_process = _process; }

  uint64_t get_and_inc_id() {
    uint64_t _id = id;
    id++;
    return _id;
  }

  const std::vector<call_path_t *> &get_call_paths() const {
    return call_paths;
  }

  void visit(BDDVisitor &visitor) const { visitor.visit(*this); }

  uint64_t get_id() const { return id; }
  void set_id(uint64_t _id) { id = _id; }

  BDDNode_ptr get_node_by_id(uint64_t _id) const {
    std::vector<BDDNode_ptr> nodes{ nf_init, nf_process };
    BDDNode_ptr node;

    while (nodes.size()) {
      node = nodes[0];
      nodes.erase(nodes.begin());

      if (node->get_id() == _id) {
        return node;
      }

      if (node->get_type() == Node::NodeType::BRANCH) {
        auto branch_node = static_cast<Branch *>(node.get());

        nodes.push_back(branch_node->get_on_true());
        nodes.push_back(branch_node->get_on_false());
      } else if (node->get_next()) {
        nodes.push_back(node->get_next());
      }
    }

    return node;
  }

  BDD clone() const {
    BDD bdd = *this;

    assert(bdd.nf_init);
    assert(bdd.nf_process);

    bdd.nf_init = bdd.nf_init->clone(true);
    bdd.nf_process = bdd.nf_process->clone(true);

    return bdd;
  }

  void rename_symbols() {
    SymbolFactory factory;

    rename_symbols(nf_init, factory);
    rename_symbols(nf_process, factory);
  }

  void trim_constraints() {
    trim_constraints(nf_init);
    trim_constraints(nf_process);
  }

  static void serialize(const BDD &bdd, std::string file_path);
  static BDD deserialize(std::string file_path);

private:
  void rename_symbols(BDDNode_ptr node, SymbolFactory &factory) {
    assert(node);

    while (node) {
      if (node->get_type() == Node::NodeType::BRANCH) {
        auto branch_node = static_cast<Branch *>(node.get());

        factory.push();
        rename_symbols(branch_node->get_on_true(), factory);
        factory.pop();

        factory.push();
        rename_symbols(branch_node->get_on_false(), factory);
        factory.pop();

        return;
      }

      if (node->get_type() == Node::NodeType::CALL) {
        auto call_node = static_cast<Call *>(node.get());
        auto call = call_node->get_call();

        factory.translate(call, node);

        node = node->get_next();
      } else {
        return;
      }
    }
  }

  void trim_constraints(BDDNode_ptr node) {
    std::vector<BDDNode_ptr> nodes{ node };

    while (nodes.size()) {
      auto node = nodes[0];
      nodes.erase(nodes.begin());

      auto available_symbols = node->get_all_generated_symbols();
      auto managers = node->get_constraints();

      std::vector<klee::ConstraintManager> new_managers;

      for (auto manager : managers) {
        klee::ConstraintManager new_manager;

        for (auto constraint : manager) {
          RetrieveSymbols retriever;
          retriever.visit(constraint);

          auto used_symbols = retriever.get_retrieved_strings();

          auto used_not_available_it =
              std::find_if(used_symbols.begin(), used_symbols.end(),
                           [&](const std::string &used_symbol) {
                auto available_symbol_it = std::find_if(
                    available_symbols.begin(), available_symbols.end(),
                    [&](const symbol_t &available_symbol) {
                      return available_symbol.label == used_symbol;
                    });

                return available_symbol_it == available_symbols.end();
              });

          if (used_not_available_it != used_symbols.end()) {
            continue;
          }

          new_manager.addConstraint(constraint);
        }

        new_managers.push_back(new_manager);
      }

      node->set_constraints(new_managers);

      if (node->get_type() == Node::NodeType::BRANCH) {
        auto branch_node = static_cast<Branch *>(node.get());

        nodes.push_back(branch_node->get_on_true());
        nodes.push_back(branch_node->get_on_false());
      } else if (node->get_next()) {
        nodes.push_back(node->get_next());
      }
    }
  }
};

} // namespace BDD
