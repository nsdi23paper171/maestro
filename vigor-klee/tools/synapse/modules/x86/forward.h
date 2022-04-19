#pragma once

#include "../../log.h"
#include "../module.h"
#include "call-paths-to-bdd.h"

namespace synapse {
namespace targets {
namespace x86 {

class Forward : public Module {
private:
  int port;

public:
  Forward() : Module(ModuleType::x86_Forward, Target::x86, "Forward") {}

  Forward(BDD::BDDNode_ptr node, int _port)
      : Module(ModuleType::x86_Forward, Target::x86, "Forward", node),
        port(_port) {}

private:
  processing_result_t
  process_return_process(const ExecutionPlan &ep, BDD::BDDNode_ptr node,
                         const BDD::ReturnProcess *casted) override {
    processing_result_t result;
    if (casted->get_return_operation() == BDD::ReturnProcess::Operation::FWD) {
      auto _port = casted->get_return_value();

      auto new_module = std::make_shared<Forward>(node, _port);
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
    auto cloned = new Forward(node, port);
    return std::shared_ptr<Module>(cloned);
  }

  virtual bool equals(const Module *other) const override {
    if (other->get_type() != type) {
      return false;
    }

    auto other_cast = static_cast<const Forward *>(other);

    if (port != other_cast->get_port()) {
      return false;
    }

    return true;
  }

  int get_port() const { return port; }
};
} // namespace x86
} // namespace targets
} // namespace synapse
