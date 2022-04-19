#pragma once

#include "../../log.h"
#include "../module.h"
#include "call-paths-to-bdd.h"

namespace synapse {
namespace targets {
namespace x86 {

class PacketGetMetadata : public Module {
private:
  klee::ref<klee::Expr> metadata;

public:
  PacketGetMetadata()
      : Module(ModuleType::x86_PacketGetMetadata, Target::x86,
               "PacketGetMetadata") {}

  PacketGetMetadata(BDD::BDDNode_ptr node, klee::ref<klee::Expr> _metadata)
      : Module(ModuleType::x86_PacketGetMetadata, Target::x86,
               "PacketGetMetadata", node),
        metadata(_metadata) {}

public:
  virtual void visit(ExecutionPlanVisitor &visitor) const override {
    visitor.visit(this);
  }

  virtual Module_ptr clone() const override {
    auto cloned = new PacketGetMetadata(node, metadata);
    return std::shared_ptr<Module>(cloned);
  }

  virtual bool equals(const Module *other) const override {
    if (other->get_type() != type) {
      return false;
    }

    auto other_cast = static_cast<const PacketGetMetadata *>(other);

    if (!BDD::solver_toolbox.are_exprs_always_equal(metadata,
                                                    other_cast->metadata)) {
      return false;
    }

    return true;
  }

  const klee::ref<klee::Expr> &get_metadata() const { return metadata; }
};
} // namespace x86
} // namespace targets
} // namespace synapse
