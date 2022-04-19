#pragma once

#include "../../log.h"
#include "../module.h"
#include "call-paths-to-bdd.h"

namespace synapse {
namespace targets {
namespace x86 {

class Broadcast : public Module {
public:
  Broadcast() : Module(ModuleType::x86_Broadcast, Target::x86, "Broadcast") {}

  Broadcast(BDD::BDDNode_ptr node)
      : Module(ModuleType::x86_Broadcast, Target::x86, "Broadcast", node) {}

private:
  processing_result_t
  process_return_process(const ExecutionPlan &ep, BDD::BDDNode_ptr node,
                         const BDD::ReturnProcess *casted) override {
    processing_result_t result;

    if (casted->get_return_operation() ==
        BDD::ReturnProcess::Operation::BCAST) {
      auto new_module = std::make_shared<Broadcast>(node);
      auto new_ep = ep.add_leaves(new_module, node->get_next(), true);

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
    auto cloned = new Broadcast(node);
    return std::shared_ptr<Module>(cloned);
  }

  virtual bool equals(const Module *other) const override {
    return other->get_type() == type;
  }
};
} // namespace x86
} // namespace targets
} // namespace synapse
