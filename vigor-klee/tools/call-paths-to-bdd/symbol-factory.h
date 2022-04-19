#pragma once

#include <unordered_set>
#include <vector>

#include "bdd-nodes.h"
#include "solver_toolbox.h"

namespace BDD {

class SymbolFactory {
public:
  static std::vector<std::string> ignored_symbols;
  static std::vector<std::string> symbols_without_translation;

  static bool should_ignore(std::string symbol);
  static bool should_not_translate(std::string symbol);

private:
  struct label_t {
    std::string base;
    std::string used;

    label_t(const std::string &_base, const std::string &_used)
        : base(_base), used(_used) {}
  };

  std::vector<std::vector<label_t>> stack;

  typedef symbols_t (SymbolFactory::*CallProcessorPtr)(
      call_t call, bool save,
      const std::vector<klee::ConstraintManager> &constraint_managers);
  std::map<std::string, CallProcessorPtr> call_processor_lookup_table;

private:
  bool has_arg(call_t call, std::string arg) {
    auto found = call.args.find(arg);
    return found != call.args.end();
  }

  bool has_extra_var(call_t call, std::string arg) {
    auto found = call.extra_vars.find(arg);
    return found != call.extra_vars.end();
  }

  int count_labels(std::string base) {
    int counter = 0;

    for (auto frame : stack) {
      for (auto label : frame) {
        if (label.base == base) {
          counter++;
        }
      }
    }

    return counter;
  }

  bool
  has_symbol(const std::vector<klee::ConstraintManager> &constraint_managers,
             const std::string &base) {
    for (auto manager : constraint_managers) {
      for (auto constraint : manager) {
        RetrieveSymbols retriever;
        retriever.visit(constraint);

        auto symbols = retriever.get_retrieved_strings();
        for (auto s : symbols) {
          if (s.find(base) == std::string::npos) {
            continue;
          }

          return true;
        }
      }
    }

    return false;
  }

  std::string
  build_label(std::string base, bool save,
              const std::vector<klee::ConstraintManager> &constraint_managers) {
    std::vector<std::string> options;

    for (auto manager : constraint_managers) {
      for (auto constraint : manager) {
        RetrieveSymbols retriever;
        retriever.visit(constraint);

        auto symbols = retriever.get_retrieved_strings();
        for (auto s : symbols) {
          if (s.find(base) == std::string::npos) {
            continue;
          }

          auto found_it = std::find(options.begin(), options.end(), s);

          if (found_it == options.end()) {
            options.push_back(s);
          }
        }
      }
    }

    std::sort(options.begin(), options.end(),
              [&](const std::string & a, const std::string & b)->bool {
      auto a_pos = a.find(base);
      auto b_pos = b.find(base);

      assert(a_pos != std::string::npos);
      assert(b_pos != std::string::npos);

      auto a_counter = a.substr(a_pos + base.size());
      auto b_counter = b.substr(b_pos + base.size());

      int a_counter_int = 0;
      int b_counter_int = 0;

      if (a_counter.size() > 1) {
        if (a_counter.find("__") != std::string::npos) {
          a_counter_int = -1;
        } else {
          a_counter = a_counter.substr(1);
          a_counter_int = std::stoi(a_counter);
        }
      }

      if (b_counter.size() > 1) {
        if (b_counter.find("__") != std::string::npos) {
          b_counter_int = -1;
        } else {
          b_counter = b_counter.substr(1);
          b_counter_int = std::stoi(b_counter);
        }
      }

      return a_counter_int < b_counter_int;
    });

    auto counter = count_labels(base);

    if (counter == 0 && options.size() == 0 && !save) {
      return base;
    }

    // hack
    if (options.size() == 0 && save && base == "checksum") {
      return base;
    }

    std::string label;

    if (counter < (int)options.size()) {
      label = options[counter];
    } else if (counter >= (int)options.size() && options.size() > 0) {
      label = options.back();
    } else {
      // I am unsure about this...
      label = base;
    }

    if (save) {
      stack.back().emplace_back(base, label);
    }

    return label;
  }

  std::string build_label(klee::ref<klee::Expr> expr, std::string base,
                          bool save) {
    RetrieveSymbols retriever;
    retriever.visit(expr);

    auto symbols = retriever.get_retrieved_strings();

    for (auto symbol : symbols) {
      auto delim = symbol.find(base);

      if (delim != std::string::npos) {
        if (save) {
          stack.back().emplace_back(base, symbol);
        }

        return symbol;
      }
    }

    std::cerr << "expr   " << expr_to_string(expr, true) << "\n";
    std::cerr << "symbol " << base << "\n";
    assert(false && "Symbol not found");
  }

  symbols_t
  no_process(call_t call, bool save,
             const std::vector<klee::ConstraintManager> &constraint_managers) {
    symbols_t symbols;
    return symbols;
  }

  symbols_t sketch_fetch(
      call_t call, bool save,
      const std::vector<klee::ConstraintManager> &constraint_managers) {
    symbols_t symbols;

    assert(!call.ret.isNull());

    auto overflow = call.ret;

    symbols.emplace(build_label(overflow, "overflow", save), "overflow",
                    overflow);

    return symbols;
  }

  symbols_t sketch_touch_buckets(
      call_t call, bool save,
      const std::vector<klee::ConstraintManager> &constraint_managers) {
    symbols_t symbols;

    assert(!call.ret.isNull());

    auto success = call.ret;

    symbols.emplace(build_label(success, "success", save), "success", success);

    return symbols;
  }

  symbols_t cht_fill_cht(
      call_t call, bool save,
      const std::vector<klee::ConstraintManager> &constraint_managers) {
    symbols_t symbols;

    assert(!call.ret.isNull());

    auto cht_fill_cht_successful = call.ret;

    symbols.emplace(
        build_label(cht_fill_cht_successful, "cht_fill_cht_successful", save),
        "cht_fill_cht_successful", cht_fill_cht_successful);

    return symbols;
  }

  symbols_t LoadBalancedFlow_hash(
      call_t call, bool save,
      const std::vector<klee::ConstraintManager> &constraint_managers) {
    symbols_t symbols;

    assert(!call.ret.isNull());

    auto hash = call.ret;

    symbols.emplace(build_label(hash, "LoadBalancedFlow_hash", save),
                    "LoadBalancedFlow_hash", hash);

    return symbols;
  }

  symbols_t cht_find_preferred_available_backend(
      call_t call, bool save,
      const std::vector<klee::ConstraintManager> &constraint_managers) {
    symbols_t symbols;

    assert(!call.ret.isNull());
    assert(!call.args["chosen_backend"].out.isNull());

    auto prefered_backend_found = call.ret;
    auto chosen_backend = call.args["chosen_backend"].out;

    symbols.emplace(
        build_label("prefered_backend_found", save, constraint_managers),
        "prefered_backend_found", prefered_backend_found);
    symbols.emplace(build_label(chosen_backend, "chosen_backend", save),
                    "chosen_backend", chosen_backend);

    return symbols;
  }

  symbols_t
  map_get(call_t call, bool save,
          const std::vector<klee::ConstraintManager> &constraint_managers) {
    symbols_t symbols;

    assert(has_arg(call, "value_out"));

    assert(!call.ret.isNull());
    assert(!call.args["value_out"].out.isNull());

    auto map_has_this_key = call.ret;
    auto value_out = call.args["value_out"].out;

    symbols.emplace(build_label("map_has_this_key", save, constraint_managers),
                    "map_has_this_key", map_has_this_key);

    auto has_this_key =
        solver_toolbox.exprBuilder->Constant(1, map_has_this_key->getWidth());
    if (solver_toolbox.are_exprs_always_equal(map_has_this_key, has_this_key)) {
      symbols.emplace(build_label(value_out, "allocated_index", save),
                      "allocated_index", value_out);
    }

    return symbols;
  }

  symbols_t dchain_is_index_allocated(
      call_t call, bool save,
      const std::vector<klee::ConstraintManager> &constraint_managers) {
    symbols_t symbols;

    assert(!call.ret.isNull());
    auto is_index_allocated = call.ret;

    symbols.emplace(
        build_label("dchain_is_index_allocated", save, constraint_managers),
        "dchain_is_index_allocated", is_index_allocated);

    return symbols;
  }

  symbols_t dchain_allocate_new_index(
      call_t call, bool save,
      const std::vector<klee::ConstraintManager> &constraint_managers) {
    symbols_t symbols;

    assert(has_arg(call, "index_out"));

    assert(!call.args["index_out"].out.isNull());
    assert(!call.ret.isNull());

    auto index_out = call.args["index_out"].out;
    auto success = call.ret;

    auto has_out_of_space = has_symbol(constraint_managers, "out_of_space");

    if (has_out_of_space) {
      symbols.emplace(build_label("out_of_space", save, constraint_managers),
                      "out_of_space", success);
    }

    symbols.emplace(build_label(index_out, "new_index", save), "new_index",
                    index_out);

    return symbols;
  }

  symbols_t packet_borrow_next_chunk(
      call_t call, bool save,
      const std::vector<klee::ConstraintManager> &constraint_managers) {
    symbols_t symbols;

    assert(has_arg(call, "chunk"));
    assert(has_extra_var(call, "the_chunk"));

    assert(!call.args["chunk"].out.isNull());
    assert(!call.extra_vars["the_chunk"].second.isNull());

    auto chunk = call.extra_vars["the_chunk"].second;
    auto chunk_addr = call.args["chunk"].out;

    symbols.emplace("packet_chunks", "packet_chunks", chunk, chunk_addr);

    return symbols;
  }

  symbols_t expire_items_single_map(
      call_t call, bool save,
      const std::vector<klee::ConstraintManager> &constraint_managers) {
    symbols_t symbols;

    assert(!call.ret.isNull());
    auto number_of_freed_flows = call.ret;

    symbols.emplace(
        build_label("number_of_freed_flows", save, constraint_managers),
        "number_of_freed_flows", number_of_freed_flows);

    return symbols;
  }

  symbols_t expire_items_single_map_iteratively(
      call_t call, bool save,
      const std::vector<klee::ConstraintManager> &constraint_managers) {
    symbols_t symbols;

    assert(!call.ret.isNull());
    auto number_of_freed_flows = call.ret;

    symbols.emplace(
        build_label("number_of_freed_flows", save, constraint_managers),
        "number_of_freed_flows", number_of_freed_flows);

    return symbols;
  }

  symbols_t rte_ether_addr_hash(
      call_t call, bool save,
      const std::vector<klee::ConstraintManager> &constraint_managers) {
    symbols_t symbols;

    assert(!call.ret.isNull());
    auto hash = call.ret;
    symbols.emplace(
        build_label("rte_ether_addr_hash", save, constraint_managers),
        "rte_ether_addr_hash", hash);

    return symbols;
  }

  symbols_t vector_borrow(
      call_t call, bool save,
      const std::vector<klee::ConstraintManager> &constraint_managers) {
    symbols_t symbols;

    assert(has_arg(call, "val_out"));
    assert(has_extra_var(call, "borrowed_cell"));

    assert(!call.args["val_out"].out.isNull());
    assert(!call.extra_vars["borrowed_cell"].second.isNull());

    auto value_out = call.args["val_out"].out;
    auto borrowed_cell = call.extra_vars["borrowed_cell"].second;

    symbols.emplace(build_label(borrowed_cell, "vector_data_reset", save),
                    "vector_data_reset", borrowed_cell, value_out);

    return symbols;
  }

  symbols_t sketch_allocate(
      call_t call, bool save,
      const std::vector<klee::ConstraintManager> &constraint_managers) {
    symbols_t symbols;

    assert(!call.ret.isNull());
    auto sketch_allocation_succeeded = call.ret;
    symbols.emplace(
        build_label("sketch_allocation_succeeded", save, constraint_managers),
        "sketch_allocation_succeeded", sketch_allocation_succeeded);

    return symbols;
  }

  symbols_t map_allocate(
      call_t call, bool save,
      const std::vector<klee::ConstraintManager> &constraint_managers) {
    symbols_t symbols;

    assert(!call.ret.isNull());
    auto map_allocation_succeeded = call.ret;
    symbols.emplace(
        build_label("map_allocation_succeeded", save, constraint_managers),
        "map_allocation_succeeded", map_allocation_succeeded);

    return symbols;
  }

  symbols_t vector_allocate(
      call_t call, bool save,
      const std::vector<klee::ConstraintManager> &constraint_managers) {
    symbols_t symbols;

    assert(!call.ret.isNull());
    auto vector_alloc_success = call.ret;
    symbols.emplace(
        build_label("vector_alloc_success", save, constraint_managers),
        "vector_alloc_success", vector_alloc_success);

    return symbols;
  }

  symbols_t current_time(
      call_t call, bool save,
      const std::vector<klee::ConstraintManager> &constraint_managers) {
    symbols_t symbols;

    assert(!call.ret.isNull());
    auto next_time = call.ret;
    symbols.emplace(build_label("next_time", save, constraint_managers),
                    "next_time", next_time);

    return symbols;
  }

  symbols_t nf_set_rte_ipv4_udptcp_checksum(
      call_t call, bool save,
      const std::vector<klee::ConstraintManager> &constraint_managers) {
    symbols_t symbols;

    klee::ref<klee::Expr> none;
    symbols.emplace(build_label("checksum", save, constraint_managers),
                    "checksum", none);

    return symbols;
  }

  symbols_t dchain_allocate(
      call_t call, bool save,
      const std::vector<klee::ConstraintManager> &constraint_managers) {
    symbols_t symbols;

    assert(!call.ret.isNull());
    auto is_dchain_allocated = call.ret;
    symbols.emplace(
        build_label("is_dchain_allocated", save, constraint_managers),
        "is_dchain_allocated", is_dchain_allocated);

    return symbols;
  }

public:
  SymbolFactory() {
    call_processor_lookup_table = {
      { "start_time", &SymbolFactory::no_process },
      { "current_time", &SymbolFactory::current_time },
      { "loop_invariant_consume", &SymbolFactory::no_process },
      { "loop_invariant_produce", &SymbolFactory::no_process },
      { "packet_receive", &SymbolFactory::no_process },
      { "packet_borrow_next_chunk", &SymbolFactory::packet_borrow_next_chunk },
      { "packet_insert_new_chunk", &SymbolFactory::no_process },
      { "packet_shrink_chunk", &SymbolFactory::no_process },
      { "packet_get_unread_length", &SymbolFactory::no_process },
      { "packet_state_total_length", &SymbolFactory::no_process },
      { "packet_return_chunk", &SymbolFactory::no_process },
      { "packet_send", &SymbolFactory::no_process },
      { "packet_free", &SymbolFactory::no_process },
      { "map_allocate", &SymbolFactory::map_allocate },
      { "map_get", &SymbolFactory::map_get },
      { "map_put", &SymbolFactory::no_process },
      { "vector_allocate", &SymbolFactory::vector_allocate },
      { "vector_borrow", &SymbolFactory::vector_borrow },
      { "vector_return", &SymbolFactory::no_process },
      { "map_erase", &SymbolFactory::no_process },
      { "dchain_allocate", &SymbolFactory::dchain_allocate },
      { "dchain_allocate_new_index",
        &SymbolFactory::dchain_allocate_new_index },
      { "dchain_is_index_allocated",
        &SymbolFactory::dchain_is_index_allocated },
      { "dchain_rejuvenate_index", &SymbolFactory::no_process },
      { "dchain_free_index", &SymbolFactory::no_process },
      { "expire_items_single_map", &SymbolFactory::expire_items_single_map },
      { "expire_items_single_map_iteratively",
        &SymbolFactory::expire_items_single_map_iteratively },
      { "sketch_allocate", &SymbolFactory::sketch_allocate },
      { "sketch_compute_hashes", &SymbolFactory::no_process },
      { "sketch_refresh", &SymbolFactory::no_process },
      { "sketch_fetch", &SymbolFactory::sketch_fetch },
      { "sketch_touch_buckets", &SymbolFactory::sketch_touch_buckets },
      { "sketch_expire", &SymbolFactory::no_process },
      { "cht_fill_cht", &SymbolFactory::cht_fill_cht },
      { "LoadBalancedFlow_hash", &SymbolFactory::LoadBalancedFlow_hash },
      { "cht_find_preferred_available_backend",
        &SymbolFactory::cht_find_preferred_available_backend },
      { "rte_ether_addr_hash", &SymbolFactory::rte_ether_addr_hash },
      { "nf_set_rte_ipv4_udptcp_checksum",
        &SymbolFactory::nf_set_rte_ipv4_udptcp_checksum },
    };

    stack.emplace_back();
  }

public:
  std::string translate_label(std::string base, const Node *node) {
    if (should_not_translate(base)) {
      return base;
    }

    std::stringstream new_label;
    new_label << base << "__" << node->get_id();
    return new_label.str();
  }

  std::string translate_label(std::string base, BDDNode_ptr node) {
    return translate_label(base, node.get());
  }

  void translate(Node *current, Node *translation_source,
                 RenameSymbols renamer) {
    assert(current);
    std::vector<Node *> nodes{ current };

    while (nodes.size()) {
      auto node = nodes[0];
      nodes.erase(nodes.begin());
      assert(node);

      if (node->get_type() == Node::NodeType::BRANCH) {
        auto branch_node = static_cast<Branch *>(node);

        auto condition = branch_node->get_condition();
        auto renamed_condition = renamer.rename(condition);

        branch_node->set_condition(renamed_condition);

        assert(branch_node->get_on_true());
        assert(branch_node->get_on_false());

        nodes.push_back(branch_node->get_on_true().get());
        nodes.push_back(branch_node->get_on_false().get());
      } else if (node->get_type() == Node::NodeType::CALL) {
        auto call_node = static_cast<Call *>(node);
        auto call = call_node->get_call();

        auto found_it = call_processor_lookup_table.find(call.function_name);

        if (found_it == call_processor_lookup_table.end()) {
          std::cerr << "\nSYMBOL FACTORY ERROR: " << call.function_name
                    << " not found in lookup table.\n";
          exit(1);
        }

        auto call_processor = found_it->second;
        auto call_symbols =
            (this->*call_processor)(call, false, node->get_constraints());

        RenameSymbols renamer_modified = renamer;
        bool modified_renamer = false;

        for (auto call_symbol : call_symbols) {
          if (translation_source->get_id() != node->get_id() &&
              renamer.has_translation(call_symbol.label)) {
            renamer_modified.remove_translation(call_symbol.label);
            modified_renamer = true;
          }
        }

        if (modified_renamer) {
          translate(node, translation_source, renamer_modified);
          continue;
        }

        for (auto &arg_pair : call.args) {
          auto &arg = call.args[arg_pair.first];

          arg.expr = renamer.rename(arg.expr);
          arg.in = renamer.rename(arg.in);
          arg.out = renamer.rename(arg.out);
        }

        for (auto &extra_var_pair : call.extra_vars) {
          auto &extra_var = call.extra_vars[extra_var_pair.first];

          extra_var.first = renamer.rename(extra_var.first);
          extra_var.second = renamer.rename(extra_var.second);
        }

        call.ret = renamer.rename(call.ret);

        call_node->set_call(call);
        assert(node->get_next());

        nodes.push_back(node->get_next().get());
      }

      auto constraints = node->get_constraints();
      auto renamed_constraints = renamer.rename(constraints);

      node->set_constraints(renamed_constraints);
    }
  }

  void translate(call_t call, BDDNode_ptr node) {
    assert(node);
    auto found_it = call_processor_lookup_table.find(call.function_name);

    if (found_it == call_processor_lookup_table.end()) {
      std::cerr << "\nSYMBOL FACTORY ERROR: " << call.function_name
                << " not found in lookup table.\n";
      exit(1);
    }

    auto call_processor = found_it->second;
    auto symbols = (this->*call_processor)(call, true, node->get_constraints());

    RenameSymbols renamer;

    for (auto symbol : symbols) {
      auto new_label = translate_label(symbol.label_base, node);

      if (new_label == symbol.label) {
        continue;
      }

      renamer.add_translation(symbol.label, new_label);
    }

    translate(node.get(), node.get(), renamer);

    assert(node->get_type() == Node::NodeType::CALL);
    auto call_node = static_cast<Call *>(node.get());
    auto generated_symbols = call_node->get_generated_symbols();

    assert(generated_symbols.size() == symbols.size());

    for (auto symbol : generated_symbols) {
      if (renamer.has_translation(symbol.label)) {
        assert(false);
      }
    }
  }

  void push() { stack.emplace_back(); }
  void pop() { stack.pop_back(); }

  symbols_t get_symbols(const Node *node) {
    if (node->get_type() != Node::NodeType::CALL) {
      return symbols_t();
    }

    auto call_node = static_cast<const Call *>(node);
    auto call = call_node->get_call();

    auto found_it = call_processor_lookup_table.find(call.function_name);

    if (found_it == call_processor_lookup_table.end()) {
      std::cerr << "\nSYMBOL FACTORY ERROR: " << call.function_name
                << " not found in lookup table.\n";
      exit(1);
    }

    auto call_processor = found_it->second;
    auto symbols =
        (this->*call_processor)(call, false, node->get_constraints());

    auto translated_symbols = symbols_t();

    for (auto &symbol : symbols) {
      auto translated_label = translate_label(symbol.label_base, node);
      translated_symbols.emplace(translated_label, symbol.label_base,
                                 symbol.expr, symbol.addr);
    }

    return translated_symbols;
  }
};
} // namespace BDD
