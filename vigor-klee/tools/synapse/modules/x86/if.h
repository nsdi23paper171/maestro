#pragma once

#include "../../log.h"
#include "../module.h"
#include "call-paths-to-bdd.h"

#include "else.h"
#include "then.h"

namespace synapse {
namespace targets {
namespace x86 {

class If : public Module {
private:
  klee::ref<klee::Expr> condition;

public:
  If() : Module(ModuleType::x86_If, Target::x86, "If") {}

  If(BDD::BDDNode_ptr node, klee::ref<klee::Expr> _condition)
      : Module(ModuleType::x86_If, Target::x86, "If", node),
        condition(_condition) {}

private:
  processing_result_t process_branch(const ExecutionPlan &ep,
                                     BDD::BDDNode_ptr node,
                                     const BDD::Branch *casted) override {
    processing_result_t result;

    assert(!casted->get_condition().isNull());
    auto _condition = casted->get_condition();

    auto new_if_module = std::make_shared<If>(node, _condition);
    auto new_then_module = std::make_shared<Then>(node);
    auto new_else_module = std::make_shared<Else>(node);

    auto if_leaf = ExecutionPlan::leaf_t(new_if_module, nullptr);
    auto then_leaf =
        ExecutionPlan::leaf_t(new_then_module, casted->get_on_true());
    auto else_leaf =
        ExecutionPlan::leaf_t(new_else_module, casted->get_on_false());

    std::vector<ExecutionPlan::leaf_t> if_leaves{ if_leaf };
    std::vector<ExecutionPlan::leaf_t> then_else_leaves{ then_leaf, else_leaf };

    auto ep_if = ep.add_leaves(if_leaves);
    auto ep_if_then_else = ep_if.add_leaves(then_else_leaves);

    result.module = new_if_module;
    result.next_eps.push_back(ep_if_then_else);

    return result;
  }

public:
  virtual void visit(ExecutionPlanVisitor &visitor) const override {
    visitor.visit(this);
  }

  virtual Module_ptr clone() const override {
    auto cloned = new If(node, condition);
    return std::shared_ptr<Module>(cloned);
  }

  virtual bool equals(const Module *other) const override {
    if (other->get_type() != type) {
      return false;
    }

    auto other_cast = static_cast<const If *>(other);

    if (!BDD::solver_toolbox.are_exprs_always_equal(
             condition, other_cast->get_condition())) {
      return false;
    }

    return true;
  }

  const klee::ref<klee::Expr> &get_condition() const { return condition; }
};
} // namespace x86
} // namespace targets
} // namespace synapse
