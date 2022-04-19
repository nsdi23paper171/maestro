#pragma once

#include "../../log.h"
#include "../module.h"
#include "call-paths-to-bdd.h"

namespace synapse {
namespace targets {
namespace x86 {

class PacketReturnChunk : public Module {
private:
  klee::ref<klee::Expr> chunk_addr;
  klee::ref<klee::Expr> chunk;

public:
  PacketReturnChunk()
      : Module(ModuleType::x86_PacketReturnChunk, Target::x86,
               "PacketReturnChunk") {}

  PacketReturnChunk(BDD::BDDNode_ptr node, klee::ref<klee::Expr> _chunk_addr,
                    klee::ref<klee::Expr> _chunk)
      : Module(ModuleType::x86_PacketReturnChunk, Target::x86,
               "PacketReturnChunk", node),
        chunk_addr(_chunk_addr), chunk(_chunk) {}

private:
  processing_result_t process_call(const ExecutionPlan &ep,
                                   BDD::BDDNode_ptr node,
                                   const BDD::Call *casted) override {
    processing_result_t result;
    auto call = casted->get_call();

    if (call.function_name == "packet_return_chunk") {
      assert(!call.args["the_chunk"].expr.isNull());
      assert(!call.args["the_chunk"].in.isNull());

      auto _chunk_addr = call.args["the_chunk"].expr;
      auto _chunk = call.args["the_chunk"].in;

      auto new_module =
          std::make_shared<PacketReturnChunk>(node, _chunk_addr, _chunk);
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
    auto cloned = new PacketReturnChunk(node, chunk_addr, chunk);
    return std::shared_ptr<Module>(cloned);
  }

  virtual bool equals(const Module *other) const override {
    if (other->get_type() != type) {
      return false;
    }

    auto other_cast = static_cast<const PacketReturnChunk *>(other);

    if (!BDD::solver_toolbox.are_exprs_always_equal(chunk,
                                                    other_cast->get_chunk())) {
      return false;
    }

    if (!BDD::solver_toolbox.are_exprs_always_equal(
             chunk_addr, other_cast->get_chunk_addr())) {
      return false;
    }

    return true;
  }

  const klee::ref<klee::Expr> &get_chunk() const { return chunk; }
  const klee::ref<klee::Expr> &get_chunk_addr() const { return chunk_addr; }
};
} // namespace x86
} // namespace targets
} // namespace synapse
