#pragma once

#include "../../log.h"
#include "../module.h"
#include "call-paths-to-bdd.h"

namespace synapse {
namespace targets {
namespace x86 {

class VectorBorrow : public Module {
private:
  klee::ref<klee::Expr> vector_addr;
  klee::ref<klee::Expr> index;
  klee::ref<klee::Expr> value_out;
  klee::ref<klee::Expr> borrowed_cell;

  BDD::symbols_t generated_symbols;

public:
  VectorBorrow()
      : Module(ModuleType::x86_VectorBorrow, Target::x86, "VectorBorrow") {}

  VectorBorrow(BDD::BDDNode_ptr node, klee::ref<klee::Expr> _vector_addr,
               klee::ref<klee::Expr> _index, klee::ref<klee::Expr> _value_out,
               klee::ref<klee::Expr> _borrowed_cell,
               BDD::symbols_t _generated_symbols)
      : Module(ModuleType::x86_VectorBorrow, Target::x86, "VectorBorrow", node),
        vector_addr(_vector_addr), index(_index), value_out(_value_out),
        borrowed_cell(_borrowed_cell), generated_symbols(_generated_symbols) {}

private:
  processing_result_t process_call(const ExecutionPlan &ep,
                                   BDD::BDDNode_ptr node,
                                   const BDD::Call *casted) override {
    processing_result_t result;
    auto call = casted->get_call();

    if (call.function_name == "vector_borrow") {
      assert(!call.args["vector"].expr.isNull());
      assert(!call.args["index"].expr.isNull());
      assert(!call.args["val_out"].out.isNull());
      assert(!call.extra_vars["borrowed_cell"].second.isNull());

      auto _vector_addr = call.args["vector"].expr;
      auto _index = call.args["index"].expr;
      auto _value_out = call.args["val_out"].out;
      auto _borrowed_cell = call.extra_vars["borrowed_cell"].second;

      auto _generated_symbols = casted->get_generated_symbols();

      auto new_module =
          std::make_shared<VectorBorrow>(node, _vector_addr, _index, _value_out,
                                         _borrowed_cell, _generated_symbols);
      auto new_ep = ep.add_leaves(new_module, node->get_next());

      result.module = new_module;
      result.next_eps.push_back(new_ep);
    }

    return result;
  }

public:
  virtual void visit(ExecutionPlanVisitor &visitor) const override {
    visitor.visit(this);
  }

  virtual Module_ptr clone() const override {
    auto cloned = new VectorBorrow(node, vector_addr, index, value_out,
                                   borrowed_cell, generated_symbols);
    return std::shared_ptr<Module>(cloned);
  }

  virtual bool equals(const Module *other) const override {
    if (other->get_type() != type) {
      return false;
    }

    auto other_cast = static_cast<const VectorBorrow *>(other);

    if (!BDD::solver_toolbox.are_exprs_always_equal(
             vector_addr, other_cast->get_vector_addr())) {
      return false;
    }

    if (!BDD::solver_toolbox.are_exprs_always_equal(index,
                                                    other_cast->get_index())) {
      return false;
    }

    if (!BDD::solver_toolbox.are_exprs_always_equal(
             value_out, other_cast->get_value_out())) {
      return false;
    }

    if (!BDD::solver_toolbox.are_exprs_always_equal(
             borrowed_cell, other_cast->get_borrowed_cell())) {
      return false;
    }

    if (generated_symbols != other_cast->generated_symbols) {
      return false;
    }

    return true;
  }

  const klee::ref<klee::Expr> &get_vector_addr() const { return vector_addr; }
  const klee::ref<klee::Expr> &get_index() const { return index; }
  const klee::ref<klee::Expr> &get_value_out() const { return value_out; }
  const klee::ref<klee::Expr> &get_borrowed_cell() const {
    return borrowed_cell;
  }

  const BDD::symbols_t &get_generated_symbols() const {
    return generated_symbols;
  }
};
} // namespace x86
} // namespace targets
} // namespace synapse
