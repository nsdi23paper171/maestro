#pragma once

#include "../../log.h"
#include "../module.h"
#include "call-paths-to-bdd.h"

namespace synapse {
namespace targets {
namespace BMv2SimpleSwitchgRPC {

class VectorReturn : public Module {
public:
  VectorReturn()
      : Module(ModuleType::BMv2SimpleSwitchgRPC_VectorReturn,
               Target::BMv2SimpleSwitchgRPC, "VectorReturn") {}

  VectorReturn(BDD::BDDNode_ptr node)
      : Module(ModuleType::BMv2SimpleSwitchgRPC_VectorReturn,
               Target::BMv2SimpleSwitchgRPC, "VectorReturn", node) {}

private:
  call_t get_previous_vector_borrow(const BDD::Node *node,
                                    klee::ref<klee::Expr> wanted_vector) const {
    while (node->get_prev()) {
      node = node->get_prev().get();

      if (node->get_type() != BDD::Node::NodeType::CALL) {
        continue;
      }

      auto call_node = static_cast<const BDD::Call *>(node);
      auto call = call_node->get_call();

      if (call.function_name != "vector_borrow") {
        continue;
      }

      auto vector = call.args["vector"].expr;
      auto eq =
          BDD::solver_toolbox.are_exprs_always_equal(vector, wanted_vector);

      if (eq) {
        return call;
      }
    }

    assert(false);
  }

  bool modifies_cell(const BDD::Call *node) const {
    auto call = node->get_call();
    assert(call.function_name == "vector_return");

    assert(!call.args["vector"].expr.isNull());
    assert(!call.args["value"].in.isNull());
    auto vector = call.args["vector"].expr;
    auto cell_after = call.args["value"].in;

    auto vector_borrow = get_previous_vector_borrow(node, vector);
    auto cell_before = vector_borrow.extra_vars["borrowed_cell"].second;

    assert(cell_before->getWidth() == cell_after->getWidth());
    auto eq =
        BDD::solver_toolbox.are_exprs_always_equal(cell_before, cell_after);

    return !eq;
  }

  processing_result_t process_call(const ExecutionPlan &ep,
                                   BDD::BDDNode_ptr node,
                                   const BDD::Call *casted) override {
    processing_result_t result;
    auto call = casted->get_call();

    if (call.function_name != "vector_return") {
      return result;
    }

    if (modifies_cell(casted)) {
      return result;
    }

    auto new_module = std::make_shared<VectorReturn>(node);
    auto new_ep =
        ep.ignore_leaf(node->get_next(), Target::BMv2SimpleSwitchgRPC);

    result.module = new_module;
    result.next_eps.push_back(new_ep);

    return result;
  }

public:
  virtual void visit(ExecutionPlanVisitor &visitor) const override {
    visitor.visit(this);
  }

  virtual Module_ptr clone() const override {
    auto cloned = new VectorReturn(node);
    return std::shared_ptr<Module>(cloned);
  }

  virtual bool equals(const Module *other) const override {
    return other->get_type() == type;
  }
};
} // namespace BMv2SimpleSwitchgRPC
} // namespace targets
} // namespace synapse
