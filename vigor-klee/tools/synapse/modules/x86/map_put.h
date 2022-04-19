#pragma once

#include "../../log.h"
#include "../module.h"
#include "call-paths-to-bdd.h"

namespace synapse {
namespace targets {
namespace x86 {

class MapPut : public Module {
private:
  klee::ref<klee::Expr> map_addr;
  klee::ref<klee::Expr> key_addr;
  klee::ref<klee::Expr> key;
  klee::ref<klee::Expr> value;

public:
  MapPut() : Module(ModuleType::x86_MapPut, Target::x86, "MapPut") {}

  MapPut(BDD::BDDNode_ptr node, klee::ref<klee::Expr> _map_addr,
         klee::ref<klee::Expr> _key_addr, klee::ref<klee::Expr> _key,
         klee::ref<klee::Expr> _value)
      : Module(ModuleType::x86_MapPut, Target::x86, "MapPut", node),
        map_addr(_map_addr), key_addr(_key_addr), key(_key), value(_value) {}

private:
  processing_result_t process_call(const ExecutionPlan &ep,
                                   BDD::BDDNode_ptr node,
                                   const BDD::Call *casted) override {
    processing_result_t result;
    auto call = casted->get_call();

    if (call.function_name == "map_put") {
      assert(!call.args["map"].expr.isNull());
      assert(!call.args["key"].expr.isNull());
      assert(!call.args["key"].in.isNull());
      assert(!call.args["value"].expr.isNull());

      auto _map_addr = call.args["map"].expr;
      auto _key_addr = call.args["key"].expr;
      auto _key = call.args["key"].in;
      auto _value = call.args["value"].expr;

      auto new_module =
          std::make_shared<MapPut>(node, _map_addr, _key_addr, _key, _value);
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
    auto cloned = new MapPut(node, map_addr, key_addr, key, value);
    return std::shared_ptr<Module>(cloned);
  }

  virtual bool equals(const Module *other) const override {
    if (other->get_type() != type) {
      return false;
    }

    auto other_cast = static_cast<const MapPut *>(other);

    if (!BDD::solver_toolbox.are_exprs_always_equal(
             map_addr, other_cast->get_map_addr())) {
      return false;
    }

    if (!BDD::solver_toolbox.are_exprs_always_equal(
             key_addr, other_cast->get_key_addr())) {
      return false;
    }

    if (!BDD::solver_toolbox.are_exprs_always_equal(key,
                                                    other_cast->get_key())) {
      return false;
    }

    if (!BDD::solver_toolbox.are_exprs_always_equal(value,
                                                    other_cast->get_value())) {
      return false;
    }

    return true;
  }

  const klee::ref<klee::Expr> &get_map_addr() const { return map_addr; }
  const klee::ref<klee::Expr> &get_key_addr() const { return key_addr; }
  const klee::ref<klee::Expr> &get_key() const { return key; }
  const klee::ref<klee::Expr> &get_value() const { return value; }
};
} // namespace x86
} // namespace targets
} // namespace synapse
