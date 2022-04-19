#pragma once

#include "../../log.h"
#include "../module.h"
#include "call-paths-to-bdd.h"

namespace synapse {
namespace targets {
namespace x86 {

class PacketGetUnreadLength : public Module {
private:
  klee::ref<klee::Expr> p_addr;
  klee::ref<klee::Expr> unread_length;

  BDD::symbols_t generated_symbols;

public:
  PacketGetUnreadLength()
      : Module(ModuleType::x86_PacketGetUnreadLength, Target::x86,
               "PacketGetUnreadLength") {}

  PacketGetUnreadLength(BDD::BDDNode_ptr node, klee::ref<klee::Expr> _p_addr,
                        klee::ref<klee::Expr> _unread_length,
                        BDD::symbols_t _generated_symbols)
      : Module(ModuleType::x86_PacketGetUnreadLength, Target::x86,
               "PacketGetUnreadLength", node),
        p_addr(_p_addr), unread_length(_unread_length),
        generated_symbols(_generated_symbols) {}

private:
  processing_result_t process_call(const ExecutionPlan &ep,
                                   BDD::BDDNode_ptr node,
                                   const BDD::Call *casted) override {
    processing_result_t result;
    auto call = casted->get_call();

    if (call.function_name == "packet_get_unread_length") {
      assert(!call.ret.isNull());
      assert(!call.args["p"].expr.isNull());

      auto _p_addr = call.args["p"].expr;
      auto _unread_length = call.ret;

      auto _generated_symbols = casted->get_generated_symbols();

      auto new_module = std::make_shared<PacketGetUnreadLength>(
          node, _p_addr, _unread_length, _generated_symbols);
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
    auto cloned = new PacketGetUnreadLength(node, p_addr, unread_length,
                                            generated_symbols);
    return std::shared_ptr<Module>(cloned);
  }

  virtual bool equals(const Module *other) const override {
    if (other->get_type() != type) {
      return false;
    }

    auto other_cast = static_cast<const PacketGetUnreadLength *>(other);

    if (!BDD::solver_toolbox.are_exprs_always_equal(p_addr,
                                                    other_cast->get_p_addr())) {
      return false;
    }

    if (!BDD::solver_toolbox.are_exprs_always_equal(
             unread_length, other_cast->get_unread_length())) {
      return false;
    }

    if (generated_symbols != other_cast->generated_symbols) {
      return false;
    }

    return true;
  }

  const klee::ref<klee::Expr> &get_p_addr() const { return p_addr; }
  const klee::ref<klee::Expr> &get_unread_length() const {
    return unread_length;
  }

  const BDD::symbols_t &get_generated_symbols() const {
    return generated_symbols;
  }
};
} // namespace x86
} // namespace targets
} // namespace synapse
