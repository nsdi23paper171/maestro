#pragma once

#include "../../log.h"
#include "../module.h"
#include "call-paths-to-bdd.h"

namespace synapse {
namespace targets {
namespace x86 {

class VectorReturn : public Module {
private:
  klee::ref<klee::Expr> vector_addr;
  klee::ref<klee::Expr> index;
  klee::ref<klee::Expr> value_addr;
  klee::ref<klee::Expr> value;

public:
  VectorReturn()
      : Module(ModuleType::x86_VectorReturn, Target::x86, "VectorReturn") {}

  VectorReturn(BDD::BDDNode_ptr node, klee::ref<klee::Expr> _vector_addr,
               klee::ref<klee::Expr> _index, klee::ref<klee::Expr> _value_addr,
               klee::ref<klee::Expr> _value)
      : Module(ModuleType::x86_VectorReturn, Target::x86, "VectorReturn", node),
        vector_addr(_vector_addr), index(_index), value_addr(_value_addr),
        value(_value) {}

private:
  processing_result_t process_call(const ExecutionPlan &ep,
                                   BDD::BDDNode_ptr node,
                                   const BDD::Call *casted) override {
    processing_result_t result;
    auto call = casted->get_call();

    if (call.function_name == "vector_return") {
      assert(!call.args["vector"].expr.isNull());
      assert(!call.args["index"].expr.isNull());
      assert(!call.args["value"].expr.isNull());
      assert(!call.args["value"].in.isNull());

      auto _vector_addr = call.args["vector"].expr;
      auto _index = call.args["index"].expr;
      auto _value_addr = call.args["value"].expr;
      auto _value = call.args["value"].in;

      auto new_module = std::make_shared<VectorReturn>(
          node, _vector_addr, _index, _value_addr, _value);
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
    auto cloned = new VectorReturn(node, vector_addr, index, value_addr, value);
    return std::shared_ptr<Module>(cloned);
  }

  virtual bool equals(const Module *other) const override {
    if (other->get_type() != type) {
      return false;
    }

    auto other_cast = static_cast<const VectorReturn *>(other);

    if (!BDD::solver_toolbox.are_exprs_always_equal(
             vector_addr, other_cast->get_vector_addr())) {
      return false;
    }

    if (!BDD::solver_toolbox.are_exprs_always_equal(index,
                                                    other_cast->get_index())) {
      return false;
    }

    if (!BDD::solver_toolbox.are_exprs_always_equal(
             value_addr, other_cast->get_value_addr())) {
      return false;
    }

    if (!BDD::solver_toolbox.are_exprs_always_equal(value,
                                                    other_cast->get_value())) {
      return false;
    }

    return true;
  }

  const klee::ref<klee::Expr> &get_vector_addr() const { return vector_addr; }
  const klee::ref<klee::Expr> &get_index() const { return index; }
  const klee::ref<klee::Expr> &get_value_addr() const { return value_addr; }
  const klee::ref<klee::Expr> &get_value() const { return value; }
};
} // namespace x86
} // namespace targets
} // namespace synapse
