#pragma once

#include "../../log.h"
#include "../module.h"
#include "call-paths-to-bdd.h"

namespace synapse {
namespace targets {
namespace x86 {

class DchainIsIndexAllocated : public Module {
private:
  klee::ref<klee::Expr> dchain_addr;
  klee::ref<klee::Expr> index;
  klee::ref<klee::Expr> is_allocated;

  BDD::symbols_t generated_symbols;

public:
  DchainIsIndexAllocated()
      : Module(ModuleType::x86_DchainIsIndexAllocated, Target::x86,
               "DchainIsIndexAllocated") {}

  DchainIsIndexAllocated(BDD::BDDNode_ptr node,
                         klee::ref<klee::Expr> _dchain_addr,
                         klee::ref<klee::Expr> _index,
                         klee::ref<klee::Expr> _is_allocated,
                         BDD::symbols_t _generated_symbols)
      : Module(ModuleType::x86_DchainIsIndexAllocated, Target::x86,
               "DchainIsIndexAllocated", node),
        dchain_addr(_dchain_addr), index(_index), is_allocated(_is_allocated),
        generated_symbols(_generated_symbols) {}

private:
  processing_result_t process_call(const ExecutionPlan &ep,
                                   BDD::BDDNode_ptr node,
                                   const BDD::Call *casted) override {
    processing_result_t result;
    auto call = casted->get_call();

    if (call.function_name == "dchain_is_index_allocated") {
      assert(!call.args["chain"].expr.isNull());
      assert(!call.args["index"].expr.isNull());
      assert(!call.ret.isNull());

      auto _dchain_addr = call.args["chain"].expr;
      auto _index = call.args["index"].expr;
      auto _is_allocated = call.ret;
      auto _generated_symbols = casted->get_generated_symbols();

      auto new_module = std::make_shared<DchainIsIndexAllocated>(
          node, _dchain_addr, _index, _is_allocated, _generated_symbols);
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
    auto cloned = new DchainIsIndexAllocated(node, dchain_addr, index,
                                             is_allocated, generated_symbols);
    return std::shared_ptr<Module>(cloned);
  }

  virtual bool equals(const Module *other) const override {
    if (other->get_type() != type) {
      return false;
    }

    auto other_cast = static_cast<const DchainIsIndexAllocated *>(other);

    if (!BDD::solver_toolbox.are_exprs_always_equal(
             dchain_addr, other_cast->get_dchain_addr())) {
      return false;
    }

    if (!BDD::solver_toolbox.are_exprs_always_equal(index,
                                                    other_cast->get_index())) {
      return false;
    }

    if (!BDD::solver_toolbox.are_exprs_always_equal(
             is_allocated, other_cast->get_is_allocated())) {
      return false;
    }

    if (generated_symbols != other_cast->generated_symbols) {
      return false;
    }

    return true;
  }

  const klee::ref<klee::Expr> &get_dchain_addr() const { return dchain_addr; }
  const klee::ref<klee::Expr> &get_index() const { return index; }
  const klee::ref<klee::Expr> &get_is_allocated() const { return is_allocated; }

  const BDD::symbols_t &get_generated_symbols() const {
    return generated_symbols;
  }
};
} // namespace x86
} // namespace targets
} // namespace synapse
