#pragma once

#include "../../log.h"
#include "../module.h"
#include "call-paths-to-bdd.h"

namespace synapse {
namespace targets {
namespace BMv2SimpleSwitchgRPC {

class Drop : public Module {
public:
  Drop()
      : Module(ModuleType::BMv2SimpleSwitchgRPC_Drop,
               Target::BMv2SimpleSwitchgRPC, "Drop") {}
  Drop(BDD::BDDNode_ptr node)
      : Module(ModuleType::BMv2SimpleSwitchgRPC_Drop,
               Target::BMv2SimpleSwitchgRPC, "Drop", node) {}

private:
  processing_result_t
  process_return_process(const ExecutionPlan &ep, BDD::BDDNode_ptr node,
                         const BDD::ReturnProcess *casted) override {
    processing_result_t result;

    if (casted->get_return_operation() == BDD::ReturnProcess::Operation::DROP) {
      auto new_module = std::make_shared<Drop>(node);
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
    auto cloned = new Drop(node);
    return std::shared_ptr<Module>(cloned);
  }

  virtual bool equals(const Module *other) const override {
    return other->get_type() == type;
  }
};
} // namespace BMv2SimpleSwitchgRPC
} // namespace targets
} // namespace synapse
