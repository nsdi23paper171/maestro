#include "call-paths-to-bdd.h"

namespace BDD {

std::vector<std::string> SymbolFactory::ignored_symbols{"VIGOR_DEVICE"};
std::vector<std::string> SymbolFactory::symbols_without_translation{
    "packet_chunks"};

bool SymbolFactory::should_ignore(std::string symbol) {
  auto found_it =
      std::find(ignored_symbols.begin(), ignored_symbols.end(), symbol);
  return found_it != ignored_symbols.end();
}

bool SymbolFactory::should_not_translate(std::string symbol) {
  auto found_it = std::find(symbols_without_translation.begin(),
                            symbols_without_translation.end(), symbol);
  return found_it != symbols_without_translation.end();
}

std::vector<std::string> call_paths_t::skip_functions{
    "loop_invariant_consume",
    "loop_invariant_produce",
    "packet_receive",
    "packet_state_total_length",
    "packet_free",
    "packet_send",
    "packet_get_unread_length"};

bool call_paths_t::is_skip_function(const std::string &fname) {
  auto found_it = std::find(call_paths_t::skip_functions.begin(),
                            call_paths_t::skip_functions.end(), fname);
  return found_it != call_paths_t::skip_functions.end();
}

klee::ref<klee::Expr>
solver_toolbox_t::create_new_symbol(const std::string &symbol_name,
                                    klee::Expr::Width width) const {
  auto domain = klee::Expr::Int32;
  auto range = klee::Expr::Int8;

  auto root = solver_toolbox.arr_cache.CreateArray(symbol_name, width, nullptr,
                                                   nullptr, domain, range);

  auto updates = klee::UpdateList(root, nullptr);
  auto read_entire_symbol = klee::ref<klee::Expr>();

  for (auto i = 0u; i < width / 8; i++) {
    auto index = exprBuilder->Constant(i, domain);

    if (read_entire_symbol.isNull()) {
      read_entire_symbol = solver_toolbox.exprBuilder->Read(updates, index);
      continue;
    }

    read_entire_symbol = exprBuilder->Concat(
        solver_toolbox.exprBuilder->Read(updates, index), read_entire_symbol);
  }

  return read_entire_symbol;
}

bool solver_toolbox_t::is_expr_always_true(klee::ref<klee::Expr> expr) const {
  klee::ConstraintManager no_constraints;
  return is_expr_always_true(no_constraints, expr);
}

bool solver_toolbox_t::is_expr_always_true(klee::ConstraintManager constraints,
                                           klee::ref<klee::Expr> expr) const {
  klee::Query sat_query(constraints, expr);

  bool result;
  bool success = solver->mustBeTrue(sat_query, result);
  assert(success);

  return result;
}

bool solver_toolbox_t::are_exprs_always_equal(
    klee::ref<klee::Expr> e1, klee::ref<klee::Expr> e2,
    klee::ConstraintManager c1, klee::ConstraintManager c2) const {
  RetrieveSymbols symbol_retriever1;
  RetrieveSymbols symbol_retriever2;

  symbol_retriever1.visit(e1);
  symbol_retriever2.visit(e2);

  auto symbols1 = symbol_retriever1.get_retrieved();
  auto symbols2 = symbol_retriever2.get_retrieved();

  ReplaceSymbols symbol_replacer1(symbols1);
  ReplaceSymbols symbol_replacer2(symbols2);

  auto eq_in_e1_ctx_expr = exprBuilder->Eq(e1, symbol_replacer1.visit(e2));
  auto eq_in_e2_ctx_expr = exprBuilder->Eq(symbol_replacer2.visit(e1), e2);

  auto eq_in_e1_ctx_sat_query = klee::Query(c1, eq_in_e1_ctx_expr);
  auto eq_in_e2_ctx_sat_query = klee::Query(c2, eq_in_e2_ctx_expr);

  bool eq_in_e1_ctx;
  bool eq_in_e2_ctx;

  bool eq_in_e1_ctx_success =
      solver->mustBeTrue(eq_in_e1_ctx_sat_query, eq_in_e1_ctx);
  bool eq_in_e2_ctx_success =
      solver->mustBeTrue(eq_in_e2_ctx_sat_query, eq_in_e2_ctx);

  assert(eq_in_e1_ctx_success);
  assert(eq_in_e2_ctx_success);

  return eq_in_e1_ctx && eq_in_e2_ctx;
}

bool solver_toolbox_t::are_exprs_always_not_equal(
    klee::ref<klee::Expr> e1, klee::ref<klee::Expr> e2,
    klee::ConstraintManager c1, klee::ConstraintManager c2) const {
  RetrieveSymbols symbol_retriever1;
  RetrieveSymbols symbol_retriever2;

  symbol_retriever1.visit(e1);
  symbol_retriever2.visit(e2);

  auto symbols1 = symbol_retriever1.get_retrieved();
  auto symbols2 = symbol_retriever2.get_retrieved();

  ReplaceSymbols symbol_replacer1(symbols1);
  ReplaceSymbols symbol_replacer2(symbols2);

  auto eq_in_e1_ctx_expr = exprBuilder->Eq(e1, symbol_replacer1.visit(e2));
  auto eq_in_e2_ctx_expr = exprBuilder->Eq(symbol_replacer2.visit(e1), e2);

  auto eq_in_e1_ctx_sat_query = klee::Query(c1, eq_in_e1_ctx_expr);
  auto eq_in_e2_ctx_sat_query = klee::Query(c2, eq_in_e2_ctx_expr);

  bool not_eq_in_e1_ctx;
  bool not_eq_in_e2_ctx;

  bool not_eq_in_e1_ctx_success =
      solver->mustBeFalse(eq_in_e1_ctx_sat_query, not_eq_in_e1_ctx);
  bool not_eq_in_e2_ctx_success =
      solver->mustBeFalse(eq_in_e2_ctx_sat_query, not_eq_in_e2_ctx);

  assert(not_eq_in_e1_ctx_success);
  assert(not_eq_in_e2_ctx_success);

  return not_eq_in_e1_ctx && not_eq_in_e2_ctx;
}

bool solver_toolbox_t::is_expr_always_true(
    klee::ConstraintManager constraints, klee::ref<klee::Expr> expr,
    ReplaceSymbols &symbol_replacer) const {
  klee::ConstraintManager replaced_constraints;

  for (auto constr : constraints) {
    replaced_constraints.addConstraint(symbol_replacer.visit(constr));
  }

  return is_expr_always_true(replaced_constraints, expr);
}

bool solver_toolbox_t::is_expr_always_false(klee::ref<klee::Expr> expr) const {
  klee::ConstraintManager no_constraints;
  return is_expr_always_false(no_constraints, expr);
}

bool solver_toolbox_t::is_expr_always_false(klee::ConstraintManager constraints,
                                            klee::ref<klee::Expr> expr) const {
  klee::Query sat_query(constraints, expr);

  bool result;
  bool success = solver->mustBeFalse(sat_query, result);
  assert(success);

  return result;
}

bool solver_toolbox_t::is_expr_always_false(
    klee::ConstraintManager constraints, klee::ref<klee::Expr> expr,
    ReplaceSymbols &symbol_replacer) const {
  klee::ConstraintManager replaced_constraints;

  for (auto constr : constraints) {
    replaced_constraints.addConstraint(symbol_replacer.visit(constr));
  }

  return is_expr_always_false(replaced_constraints, expr);
}

bool solver_toolbox_t::are_exprs_always_equal(
    klee::ref<klee::Expr> expr1, klee::ref<klee::Expr> expr2) const {
  if (expr1.isNull() != expr2.isNull()) {
    return false;
  }

  if (expr1.isNull()) {
    return true;
  }

  if (expr1->getWidth() != expr2->getWidth()) {
    return false;
  }

  RetrieveSymbols symbol_retriever;
  symbol_retriever.visit(expr1);
  std::vector<klee::ref<klee::ReadExpr>> symbols =
      symbol_retriever.get_retrieved();

  ReplaceSymbols symbol_replacer(symbols);
  klee::ref<klee::Expr> replaced = symbol_replacer.visit(expr2);

  assert(exprBuilder);
  assert(!replaced.isNull());

  auto eq = exprBuilder->Eq(expr1, replaced);
  return is_expr_always_true(eq);
}

uint64_t solver_toolbox_t::value_from_expr(klee::ref<klee::Expr> expr) const {
  klee::ConstraintManager no_constraints;
  klee::Query sat_query(no_constraints, expr);

  klee::ref<klee::ConstantExpr> value_expr;
  bool success = solver->getValue(sat_query, value_expr);

  assert(success);
  return value_expr->getZExtValue();
}

uint64_t
solver_toolbox_t::value_from_expr(klee::ref<klee::Expr> expr,
                                  klee::ConstraintManager constraints) const {
  klee::Query sat_query(constraints, expr);

  klee::ref<klee::ConstantExpr> value_expr;
  bool success = solver->getValue(sat_query, value_expr);

  assert(success);
  return value_expr->getZExtValue();
}

bool solver_toolbox_t::are_calls_equal(call_t c1, call_t c2) const {
  if (c1.function_name != c2.function_name) {
    return false;
  }

  for (auto arg : c1.args) {
    auto found = c2.args.find(arg.first);
    if (found == c2.args.end()) {
      return false;
    }

    auto arg1 = arg.second;
    auto arg2 = found->second;

    auto expr1 = arg1.expr;
    auto expr2 = arg2.expr;

    auto in1 = arg1.in;
    auto in2 = arg2.in;

    auto out1 = arg1.out;
    auto out2 = arg2.out;

    if (expr1.isNull() != expr2.isNull()) {
      return false;
    }

    if (in1.isNull() != in2.isNull()) {
      return false;
    }

    if (out1.isNull() != out2.isNull()) {
      return false;
    }

    if (in1.isNull() && out1.isNull() &&
        !are_exprs_always_equal(expr1, expr2)) {
      return false;
    }

    if (!in1.isNull() && !are_exprs_always_equal(in1, in2)) {
      return false;
    }
  }

  return true;
}

symbols_t Node::get_all_generated_symbols() const {
  symbols_t symbols;
  const Node *node = this;

  // hack: symbols always known
  klee::ref<klee::Expr> empty_expr;
  symbols.emplace("VIGOR_DEVICE", "VIGOR_DEVICE", empty_expr);
  symbols.emplace("pkt_len", "pkt_len", empty_expr);
  symbols.emplace("data_len", "data_len", empty_expr);
  symbols.emplace("received_a_packet", "received_a_packet", empty_expr);

  while (node) {
    if (node->get_type() == Node::NodeType::CALL) {
      const Call *call = static_cast<const Call *>(node);
      auto more_symbols = call->get_generated_symbols();

      for (auto symbol : more_symbols) {
        symbols.insert(symbol);
      }
    }

    node = node->get_prev().get();
  }

  return symbols;
}

symbols_t Call::get_generated_symbols() const {
  SymbolFactory symbol_factory;
  return symbol_factory.get_symbols(this);
}

void Call::recursive_update_ids(uint64_t &new_id) {
  update_id(new_id);
  new_id++;
  next->recursive_update_ids(new_id);
}

void Branch::recursive_update_ids(uint64_t &new_id) {
  update_id(new_id);
  new_id++;
  next->recursive_update_ids(new_id);
  on_false->recursive_update_ids(new_id);
}

void ReturnRaw::recursive_update_ids(uint64_t &new_id) {
  update_id(new_id);
  new_id++;
}

void ReturnInit::recursive_update_ids(uint64_t &new_id) {
  update_id(new_id);
  new_id++;
}

void ReturnProcess::recursive_update_ids(uint64_t &new_id) {
  update_id(new_id);
  new_id++;
}

void Node::update_id(uint64_t new_id) {
  SymbolFactory factory;
  auto symbols = factory.get_symbols(this);

  id = new_id;

  if (symbols.size() == 0) {
    return;
  }

  RenameSymbols renamer;
  for (auto symbol : symbols) {
    auto new_label = factory.translate_label(symbol.label_base, this);

    if (new_label == symbol.label) {
      continue;
    }

    renamer.add_translation(symbol.label, new_label);
  }

  if (renamer.get_translations().size() == 0) {
    return;
  }

  factory.translate(this, this, renamer);
}

void CallPathsGroup::group_call_paths() {
  assert(call_paths.size());

  for (const auto &cp : call_paths.cp) {
    on_true.clear();
    on_false.clear();

    if (cp->calls.size() == 0) {
      continue;
    }

    call_t call = cp->calls[0];

    for (unsigned int icp = 0; icp < call_paths.size(); icp++) {
      auto pair = call_paths.get(icp);

      if (pair.first->calls.size() &&
          are_calls_equal(pair.first->calls[0], call)) {
        on_true.push_back(pair);
        continue;
      }

      on_false.push_back(pair);
    }

    // all calls are equal, no need do discriminate
    if (on_false.size() == 0) {
      return;
    }

    constraint = find_discriminating_constraint();

    if (!constraint.isNull()) {
      return;
    }
  }

  // no more calls
  if (on_true.size() == 0 && on_false.size() == 0) {
    on_true = call_paths;
    return;
  }

  // Last shot...
  for (int i = 0; i < call_paths.cp.size(); i++) {
    on_true.clear();
    on_false.clear();

    for (int j = 0; j < call_paths.cp.size(); j++) {
      auto pair = call_paths.get(j);

      if (i == j) {
        on_true.push_back(pair);
      } else {
        on_false.push_back(pair);
      }
    }

    constraint = find_discriminating_constraint();

    if (!constraint.isNull()) {
      return;
    }
  }

  assert(false && "Could not group call paths");
}

bool CallPathsGroup::are_calls_equal(call_t c1, call_t c2) {
  if (c1.function_name != c2.function_name) {
    return false;
  }

  for (auto arg_name_value_pair : c1.args) {
    auto arg_name = arg_name_value_pair.first;

    // exception: we don't care about 'p' differences
    if (arg_name == "p" || arg_name == "src_devices") {
      continue;
    }

    auto c1_arg = c1.args[arg_name];
    auto c2_arg = c2.args[arg_name];

    if (!c1_arg.out.isNull() &&
        !solver_toolbox.are_exprs_always_equal(c1_arg.in, c1_arg.out)) {
      continue;
    }

    // comparison between modifications to the received packet
    if (!c1_arg.in.isNull() &&
        !solver_toolbox.are_exprs_always_equal(c1_arg.in, c2_arg.in)) {
      return false;
    }

    if (c1_arg.in.isNull() &&
        !solver_toolbox.are_exprs_always_equal(c1_arg.expr, c2_arg.expr)) {
      return false;
    }
  }

  return true;
}

klee::ref<klee::Expr> CallPathsGroup::find_discriminating_constraint() {
  assert(on_true.size());

  auto possible_discriminating_constraints =
      get_possible_discriminating_constraints();

  for (auto constraint : possible_discriminating_constraints) {
    if (check_discriminating_constraint(constraint)) {
      return constraint;
    }
  }

  return klee::ref<klee::Expr>();
}

std::vector<klee::ref<klee::Expr>>
CallPathsGroup::get_possible_discriminating_constraints() const {
  std::vector<klee::ref<klee::Expr>> possible_discriminating_constraints;
  assert(on_true.size());

  int i = 0;
  for (auto constraint : on_true.cp[0]->constraints) {
    if (satisfies_constraint(on_true.cp, constraint)) {
      possible_discriminating_constraints.emplace_back(constraint);
    }
    i++;
  }

  return possible_discriminating_constraints;
}

bool CallPathsGroup::satisfies_constraint(
    std::vector<call_path_t *> call_paths,
    klee::ref<klee::Expr> constraint) const {
  for (const auto &call_path : call_paths) {
    if (!satisfies_constraint(call_path, constraint)) {
      return false;
    }
  }
  return true;
}

bool CallPathsGroup::satisfies_constraint(
    call_path_t *call_path, klee::ref<klee::Expr> constraint) const {
  RetrieveSymbols symbol_retriever;
  symbol_retriever.visit(constraint);
  std::vector<klee::ref<klee::ReadExpr>> symbols =
      symbol_retriever.get_retrieved();

  ReplaceSymbols symbol_replacer(symbols);
  auto not_constraint = solver_toolbox.exprBuilder->Not(constraint);

  return solver_toolbox.is_expr_always_false(call_path->constraints,
                                             not_constraint, symbol_replacer);
}

bool CallPathsGroup::satisfies_not_constraint(
    std::vector<call_path_t *> call_paths,
    klee::ref<klee::Expr> constraint) const {
  for (const auto &call_path : call_paths) {
    if (!satisfies_not_constraint(call_path, constraint)) {
      return false;
    }
  }
  return true;
}

bool CallPathsGroup::satisfies_not_constraint(
    call_path_t *call_path, klee::ref<klee::Expr> constraint) const {
  RetrieveSymbols symbol_retriever;
  symbol_retriever.visit(constraint);
  std::vector<klee::ref<klee::ReadExpr>> symbols =
      symbol_retriever.get_retrieved();

  ReplaceSymbols symbol_replacer(symbols);
  auto not_constraint = solver_toolbox.exprBuilder->Not(constraint);

  return solver_toolbox.is_expr_always_true(call_path->constraints,
                                            not_constraint, symbol_replacer);
}

bool CallPathsGroup::check_discriminating_constraint(
    klee::ref<klee::Expr> constraint) {
  assert(on_true.size());
  assert(on_false.size());

  call_paths_t _on_true = on_true;
  call_paths_t _on_false;

  for (unsigned int i = 0; i < on_false.size(); i++) {
    auto pair = on_false.get(i);
    auto call_path = pair.first;

    if (satisfies_constraint(call_path, constraint)) {
      _on_true.push_back(pair);
    } else {
      _on_false.push_back(pair);
    }
  }

  if (_on_false.size() && satisfies_not_constraint(_on_false.cp, constraint)) {
    on_true = _on_true;
    on_false = _on_false;
    return true;
  }

  return false;
}

constexpr char BDD::INIT_CONTEXT_MARKER[];

solver_toolbox_t solver_toolbox;

std::vector<std::string> BDD::skip_conditions_with_symbol{"received_a_packet",
                                                          "loop_termination"};

std::string BDD::get_fname(const Node *node) {
  assert(node->get_type() == Node::NodeType::CALL);
  const Call *call = static_cast<const Call *>(node);
  return call->get_call().function_name;
}

bool BDD::is_skip_function(const Node *node) {
  auto fname = BDD::get_fname(node);
  return call_paths_t::is_skip_function(fname);
}

bool BDD::is_skip_condition(const Node *node) {
  assert(node->get_type() == Node::NodeType::BRANCH);
  const Branch *branch = static_cast<const Branch *>(node);
  auto cond = branch->get_condition();

  RetrieveSymbols retriever;
  retriever.visit(cond);

  auto symbols = retriever.get_retrieved_strings();
  for (const auto &symbol : symbols) {
    auto found_it = std::find(BDD::skip_conditions_with_symbol.begin(),
                              BDD::skip_conditions_with_symbol.end(), symbol);
    if (found_it != BDD::skip_conditions_with_symbol.end()) {
      return true;
    }
  }

  return false;
}

call_t BDD::get_successful_call(std::vector<call_path_t *> call_paths) const {
  assert(call_paths.size());

  for (const auto &cp : call_paths) {
    assert(cp->calls.size());
    call_t call = cp->calls[0];

    if (call.ret.isNull()) {
      return call;
    }

    auto zero = solver_toolbox.exprBuilder->Constant(0, call.ret->getWidth());
    auto eq_zero = solver_toolbox.exprBuilder->Eq(call.ret, zero);
    auto is_ret_success = solver_toolbox.is_expr_always_false(eq_zero);

    if (is_ret_success) {
      return call;
    }
  }

  // no function with successful return
  return call_paths[0]->calls[0];
}

BDDNode_ptr BDD::populate(call_paths_t call_paths) {
  BDDNode_ptr local_root = nullptr;
  BDDNode_ptr local_leaf = nullptr;

  auto return_raw = std::make_shared<ReturnRaw>(get_and_inc_id(), call_paths);

  while (call_paths.cp.size()) {
    CallPathsGroup group(call_paths);

    auto on_true = group.get_on_true();
    auto on_false = group.get_on_false();

    if (on_true.cp.size() == call_paths.cp.size()) {
      assert(on_false.cp.size() == 0);

      if (on_true.cp[0]->calls.size() == 0) {
        break;
      }

      auto call = get_successful_call(on_true.cp);
      auto node = std::make_shared<Call>(get_and_inc_id(), call, on_true.cp);

      // root node
      if (local_root == nullptr) {
        local_root = node;
        local_leaf = node;
      } else {
        local_leaf->add_next(node);
        node->add_prev(local_leaf);

        assert(node->get_prev());
        assert(node->get_prev()->get_id() == local_leaf->get_id());

        local_leaf = node;
      }

      for (auto &cp : call_paths.cp) {
        assert(cp->calls.size());
        cp->calls.erase(cp->calls.begin());
      }
    } else {
      auto discriminating_constraint = group.get_discriminating_constraint();

      auto node = std::make_shared<Branch>(
          get_and_inc_id(), discriminating_constraint, call_paths.cp);

      auto on_true_root = populate(on_true);
      auto on_false_root = populate(on_false);

      node->add_on_true(on_true_root);
      node->add_on_false(on_false_root);

      on_true_root->replace_prev(node);
      on_false_root->replace_prev(node);

      assert(on_true_root->get_prev());
      assert(on_true_root->get_prev()->get_id() == node->get_id());

      assert(on_false_root->get_prev());
      assert(on_false_root->get_prev()->get_id() == node->get_id());

      if (local_root == nullptr) {
        return node;
      }

      local_leaf->add_next(node);
      node->add_prev(local_leaf);

      assert(node->get_prev());
      assert(node->get_prev()->get_id() == local_leaf->get_id());

      return local_root;
    }
  }

  if (local_root == nullptr) {
    local_root = return_raw;
  } else {
    local_leaf->add_next(return_raw);
    return_raw->add_prev(local_leaf);

    assert(return_raw->get_prev());
    assert(return_raw->get_prev()->get_id() == local_leaf->get_id());
  }

  return local_root;
}

BDDNode_ptr BDD::populate_init(const BDDNode_ptr &root) {
  Node *node = root.get();
  assert(node);

  BDDNode_ptr local_root;
  BDDNode_ptr local_leaf;
  BDDNode_ptr new_node;

  bool build_return = true;

  while (node) {
    new_node = nullptr;

    switch (node->get_type()) {
    case Node::NodeType::CALL: {
      if (get_fname(node) == BDD::INIT_CONTEXT_MARKER) {
        node = nullptr;
        break;
      }

      if (!is_skip_function(node)) {
        BDDNode_ptr empty;

        new_node = node->clone();
        new_node->replace_next(empty);
        new_node->replace_prev(empty);
        assert(new_node);
      }

      node = node->get_next().get();
      assert(node);
      break;
    };
    case Node::NodeType::BRANCH: {
      auto root_branch = static_cast<const Branch *>(node);

      auto on_true_node = populate_init(root_branch->get_on_true());
      auto on_false_node = populate_init(root_branch->get_on_false());

      assert(on_true_node);
      assert(on_false_node);

      auto cloned = node->clone();
      auto branch = static_cast<Branch *>(cloned.get());

      branch->replace_on_true(on_true_node);
      branch->replace_on_false(on_false_node);

      on_true_node->replace_prev(cloned);
      on_false_node->replace_prev(cloned);

      assert(on_true_node->get_prev());
      assert(on_true_node->get_prev()->get_id() == branch->get_id());

      assert(on_false_node->get_prev());
      assert(on_false_node->get_prev()->get_id() == branch->get_id());

      new_node = cloned;
      node = nullptr;
      build_return = false;

      break;
    };
    case Node::NodeType::RETURN_RAW: {
      auto root_return_raw = static_cast<const ReturnRaw *>(node);
      new_node =
          std::make_shared<ReturnInit>(get_and_inc_id(), root_return_raw);

      node = nullptr;
      build_return = false;
      break;
    };
    default: {
      assert(false && "Should not encounter return nodes here");
    };
    }

    if (new_node && local_leaf == nullptr) {
      local_root = new_node;
      local_leaf = local_root;
    } else if (new_node) {
      local_leaf->replace_next(new_node);
      new_node->replace_prev(local_leaf);

      assert(new_node->get_prev());
      assert(new_node->get_prev()->get_id() == local_leaf->get_id());

      local_leaf = new_node;
    }
  }

  if (local_root == nullptr) {
    local_root = std::make_shared<ReturnInit>(get_and_inc_id());
  }

  if (build_return && local_leaf) {
    auto ret = std::make_shared<ReturnInit>(get_and_inc_id());

    local_leaf->replace_next(ret);
    ret->replace_prev(local_leaf);

    assert(ret->get_prev());
    assert(ret->get_prev()->get_id() == local_leaf->get_id());
  }

  return local_root;
}

BDDNode_ptr BDD::populate_process(const BDDNode_ptr &root, bool store) {
  Node *node = root.get();

  BDDNode_ptr local_root;
  BDDNode_ptr local_leaf;
  BDDNode_ptr new_node;

  while (node != nullptr) {
    new_node = nullptr;

    switch (node->get_type()) {
    case Node::NodeType::CALL: {
      if (get_fname(node) == BDD::INIT_CONTEXT_MARKER) {
        store = true;
        node = node->get_next().get();
        break;
      }

      if (store && !is_skip_function(node)) {
        BDDNode_ptr empty;

        new_node = node->clone();
        new_node->replace_next(empty);
        new_node->replace_prev(empty);
      }

      node = node->get_next().get();
      break;
    };
    case Node::NodeType::BRANCH: {
      auto root_branch = static_cast<const Branch *>(node);
      assert(root_branch->get_on_true());
      assert(root_branch->get_on_false());

      auto on_true_node = populate_process(root_branch->get_on_true(), store);
      auto on_false_node = populate_process(root_branch->get_on_false(), store);

      assert(on_true_node);
      assert(on_false_node);

      auto skip = is_skip_condition(node);
      auto equal = false;

      if (on_true_node->get_type() == Node::NodeType::RETURN_PROCESS &&
          on_false_node->get_type() == Node::NodeType::RETURN_PROCESS) {

        auto on_true_ret_process =
            static_cast<ReturnProcess *>(on_true_node.get());
        auto on_false_ret_process =
            static_cast<ReturnProcess *>(on_false_node.get());

        equal |= (on_true_ret_process->get_return_operation() ==
                      on_false_ret_process->get_return_operation() &&
                  on_true_ret_process->get_return_value() ==
                      on_false_ret_process->get_return_value());
      }

      if (store && equal) {
        new_node = on_true_node;
      }

      else if (store && !skip) {
        auto clone = node->clone();
        auto branch = static_cast<Branch *>(clone.get());

        branch->replace_on_true(on_true_node);
        branch->replace_on_false(on_false_node);

        on_true_node->replace_prev(clone);
        on_false_node->replace_prev(clone);

        assert(on_true_node->get_prev());
        assert(on_true_node->get_prev()->get_id() == branch->get_id());

        assert(on_false_node->get_prev());
        assert(on_false_node->get_prev()->get_id() == branch->get_id());

        new_node = clone;
      }

      else {
        auto on_true_empty =
            on_true_node->get_type() == Node::NodeType::RETURN_INIT ||
            on_true_node->get_type() == Node::NodeType::RETURN_PROCESS;

        auto on_false_empty =
            on_false_node->get_type() == Node::NodeType::RETURN_INIT ||
            on_false_node->get_type() == Node::NodeType::RETURN_PROCESS;

        if (on_true_node->get_type() == Node::NodeType::RETURN_PROCESS) {
          auto on_true_return_process =
              static_cast<ReturnProcess *>(on_true_node.get());
          on_true_empty |= (on_true_return_process->get_return_operation() ==
                            ReturnProcess::Operation::ERR);
        }

        if (on_false_node->get_type() == Node::NodeType::RETURN_PROCESS) {
          auto on_false_return_process =
              static_cast<ReturnProcess *>(on_false_node.get());
          on_false_empty |= (on_false_return_process->get_return_operation() ==
                             ReturnProcess::Operation::ERR);
        }

        assert(on_true_empty || on_false_empty);
        new_node = on_false_empty ? on_true_node : on_false_node;
      }

      node = nullptr;
      break;
    };
    case Node::NodeType::RETURN_RAW: {
      auto root_return_raw = static_cast<const ReturnRaw *>(node);
      new_node =
          std::make_shared<ReturnProcess>(get_and_inc_id(), root_return_raw);

      node = nullptr;
      break;
    };
    default: {
      assert(false && "Should not encounter return nodes here");
    };
    }

    if (new_node && local_leaf == nullptr) {
      local_root = new_node;
      local_leaf = new_node;
    } else if (new_node) {
      local_leaf->replace_next(new_node);
      new_node->replace_prev(local_leaf);

      local_leaf = new_node;
    }
  }

  assert(local_root);
  return local_root;
}

void BDDVisitor::visit(const Branch *node) {
  if (!node)
    return;

  auto action = visitBranch(node);

  if (action == VISIT_CHILDREN) {
    node->get_on_true()->visit(*this);
    node->get_on_false()->visit(*this);
  }
}

void BDDVisitor::visit(const Call *node) {
  if (!node)
    return;

  auto action = visitCall(node);

  if (action == VISIT_CHILDREN) {
    node->get_next()->visit(*this);
  }
}

void BDDVisitor::visit(const ReturnInit *node) {
  if (!node)
    return;

  visitReturnInit(node);
  assert(!node->get_next());
}

void BDDVisitor::visit(const ReturnProcess *node) {
  if (!node)
    return;

  visitReturnProcess(node);
  assert(!node->get_next());
}

void BDDVisitor::visit(const ReturnRaw *node) {
  if (!node)
    return;

  visitReturnRaw(node);
  assert(!node->get_next());
}

void BDDVisitor::visit(const BDD &bdd) {
  assert(bdd.get_init());
  visitInitRoot(bdd.get_init().get());

  assert(bdd.get_process());
  visitProcessRoot(bdd.get_process().get());
}

void BDDVisitor::visitInitRoot(const Node *root) {
  if (!root)
    return;
  root->visit(*this);
}

void BDDVisitor::visitProcessRoot(const Node *root) {
  if (!root)
    return;
  root->visit(*this);
}

} // namespace BDD
