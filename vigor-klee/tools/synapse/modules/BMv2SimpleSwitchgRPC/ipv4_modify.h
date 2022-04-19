#pragma once

#include "../../log.h"
#include "../module.h"
#include "call-paths-to-bdd.h"

#include "ignore.h"

namespace synapse {
namespace targets {
namespace BMv2SimpleSwitchgRPC {

class IPv4Modify : public Module {
private:
  std::vector<modification_t> modifications;

public:
  IPv4Modify()
      : Module(ModuleType::BMv2SimpleSwitchgRPC_IPv4Modify,
               Target::BMv2SimpleSwitchgRPC, "IPv4Modify") {}

  IPv4Modify(BDD::BDDNode_ptr node,
             const std::vector<modification_t> &_modifications)
      : Module(ModuleType::BMv2SimpleSwitchgRPC_IPv4Modify,
               Target::BMv2SimpleSwitchgRPC, "IPv4Modify", node),
        modifications(_modifications) {}

private:
  klee::ref<klee::Expr> get_ipv4_chunk(const BDD::Node *node) const {
    assert(node->get_type() == BDD::Node::NodeType::CALL);

    auto call_node = static_cast<const BDD::Call *>(node);
    auto call = call_node->get_call();

    assert(call.function_name == "packet_borrow_next_chunk");
    assert(!call.extra_vars["the_chunk"].second.isNull());

    return call.extra_vars["the_chunk"].second;
  }

  processing_result_t process_call(const ExecutionPlan &ep,
                                   BDD::BDDNode_ptr node,
                                   const BDD::Call *casted) override {
    processing_result_t result;
    auto call = casted->get_call();

    if (call.function_name != "packet_return_chunk") {
      return result;
    }

    auto all_prev_packet_borrow_next_chunk =
        get_all_prev_functions(casted, "packet_borrow_next_chunk");

    assert(all_prev_packet_borrow_next_chunk.size());

    auto all_prev_packet_return_chunk =
        get_all_prev_functions(casted, "packet_return_chunk");

    if (all_prev_packet_return_chunk.size() !=
        all_prev_packet_borrow_next_chunk.size() - 2) {
      return result;
    }

    assert(!call.args["the_chunk"].in.isNull());

    auto borrow_ipv4 = all_prev_packet_borrow_next_chunk.rbegin()[1];

    auto curr_ipv4_chunk = call.args["the_chunk"].in;
    auto prev_ipv4_chunk = get_ipv4_chunk(borrow_ipv4.get());

    assert(curr_ipv4_chunk->getWidth() == 20 * 8);
    assert(prev_ipv4_chunk->getWidth() == 20 * 8);

    auto _modifications = build_modifications(prev_ipv4_chunk, curr_ipv4_chunk);

    if (_modifications.size() == 0) {
      auto new_module = std::make_shared<Ignore>(node);
      auto new_ep =
          ep.ignore_leaf(node->get_next(), Target::BMv2SimpleSwitchgRPC);

      result.module = new_module;
      result.next_eps.push_back(new_ep);

      return result;
    }

    auto new_module = std::make_shared<IPv4Modify>(node, _modifications);
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
    auto cloned = new IPv4Modify(node, modifications);
    return std::shared_ptr<Module>(cloned);
  }

  virtual bool equals(const Module *other) const override {
    if (other->get_type() != type) {
      return false;
    }

    auto other_cast = static_cast<const IPv4Modify *>(other);

    auto other_modifications = other_cast->get_modifications();

    if (modifications.size() != other_modifications.size()) {
      return false;
    }

    for (unsigned i = 0; i < modifications.size(); i++) {
      auto modification = modifications[i];
      auto other_modification = other_modifications[i];

      if (modification.byte != other_modification.byte) {
        return false;
      }

      if (!BDD::solver_toolbox.are_exprs_always_equal(
               modification.expr, other_modification.expr)) {
        return false;
      }
    }

    return true;
  }

  const std::vector<modification_t> &get_modifications() const {
    return modifications;
  }
};
} // namespace BMv2SimpleSwitchgRPC
} // namespace targets
} // namespace synapse
