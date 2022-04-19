#pragma once

#include "../../log.h"
#include "../module.h"
#include "call-paths-to-bdd.h"

namespace synapse {
namespace targets {
namespace BMv2SimpleSwitchgRPC {

class EthernetConsume : public Module {
private:
  klee::ref<klee::Expr> chunk;

public:
  EthernetConsume()
      : Module(ModuleType::BMv2SimpleSwitchgRPC_EthernetConsume,
               Target::BMv2SimpleSwitchgRPC, "EthernetConsume") {}

  EthernetConsume(BDD::BDDNode_ptr node, klee::ref<klee::Expr> _chunk)
      : Module(ModuleType::BMv2SimpleSwitchgRPC_EthernetConsume,
               Target::BMv2SimpleSwitchgRPC, "EthernetConsume", node),
        chunk(_chunk) {}

private:
  processing_result_t process_call(const ExecutionPlan &ep,
                                   BDD::BDDNode_ptr node,
                                   const BDD::Call *casted) override {
    processing_result_t result;
    auto call = casted->get_call();

    if (call.function_name != "packet_borrow_next_chunk") {
      return result;
    }

    auto all_prev_packet_borrow_next_chunk =
        get_all_prev_functions(casted, "packet_borrow_next_chunk");

    if (all_prev_packet_borrow_next_chunk.size() != 0) {
      return result;
    }

    assert(!call.args["length"].expr.isNull());
    assert(!call.extra_vars["the_chunk"].second.isNull());

    auto _length = call.args["length"].expr;
    auto _chunk = call.extra_vars["the_chunk"].second;

    // Make sure that packet_borrow_next_chunk borrows the
    // 14 ethernet bytes
    assert(_length->getKind() == klee::Expr::Kind::Constant);
    assert(BDD::solver_toolbox.value_from_expr(_length) == 14);

    auto new_module = std::make_shared<EthernetConsume>(node, _chunk);
    auto new_ep = ep.add_leaves(new_module, node->get_next());

    result.module = new_module;
    result.next_eps.push_back(new_ep);

    return result;
  }

public:
  virtual void visit(ExecutionPlanVisitor &visitor) const override {
    visitor.visit(this);
  }

  virtual Module_ptr clone() const override {
    auto cloned = new EthernetConsume(node, chunk);
    return std::shared_ptr<Module>(cloned);
  }

  virtual bool equals(const Module *other) const override {
    return other->get_type() == type;
  }

  const klee::ref<klee::Expr> &get_chunk() const { return chunk; }
};
} // namespace BMv2SimpleSwitchgRPC
} // namespace targets
} // namespace synapse
