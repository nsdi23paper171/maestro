#pragma once

#include "../../log.h"
#include "../module.h"
#include "call-paths-to-bdd.h"

#include <netinet/in.h>

namespace synapse {
namespace targets {
namespace BMv2SimpleSwitchgRPC {

class IPOptionsConsume : public Module {
private:
  klee::ref<klee::Expr> chunk;
  klee::ref<klee::Expr> length;

public:
  IPOptionsConsume()
      : Module(ModuleType::BMv2SimpleSwitchgRPC_IPOptionsConsume,
               Target::BMv2SimpleSwitchgRPC, "IPOptionsConsume") {}

  IPOptionsConsume(BDD::BDDNode_ptr node, klee::ref<klee::Expr> _chunk,
                   klee::ref<klee::Expr> _length)
      : Module(ModuleType::BMv2SimpleSwitchgRPC_IPOptionsConsume,
               Target::BMv2SimpleSwitchgRPC, "IPOptionsConsume", node),
        chunk(_chunk), length(_length) {}

private:
  bool always_true(klee::ref<klee::Expr> expr,
                   const std::vector<klee::ConstraintManager> &constraints) {
    RetrieveSymbols symbol_retriever;
    symbol_retriever.visit(expr);
    auto symbols = symbol_retriever.get_retrieved();
    BDD::ReplaceSymbols symbol_replacer(symbols);

    for (auto constraint : constraints) {
      if (!BDD::solver_toolbox.is_expr_always_true(constraint, expr,
                                                   symbol_replacer)) {
        return false;
      }
    }

    return true;
  }

  bool is_valid_ipv4(const BDD::Node *ethernet_node,
                     const std::vector<klee::ConstraintManager> &constraints) {
    assert(ethernet_node);
    assert(ethernet_node->get_type() == BDD::Node::NodeType::CALL);

    auto call_node = static_cast<const BDD::Call *>(ethernet_node);
    auto call = call_node->get_call();

    auto ethernet_chunk = call.extra_vars["the_chunk"].second;

    assert(!ethernet_chunk.isNull());

    auto eth_type_expr =
        BDD::solver_toolbox.exprBuilder->Extract(ethernet_chunk, 12 * 8, 2 * 8);
    auto eth_type_ipv4 = BDD::solver_toolbox.exprBuilder->Constant(
        UINT_16_SWAP_ENDIANNESS(0x0800), 2 * 8);
    auto eq = BDD::solver_toolbox.exprBuilder->Eq(eth_type_expr, eth_type_ipv4);

    return always_true(eq, constraints);
  }

  bool
  is_valid_ip_options(const BDD::Node *ip_options_node,
                      klee::ref<klee::Expr> len,
                      const std::vector<klee::ConstraintManager> &constraints) {
    assert(ip_options_node);
    assert(!len.isNull());

    // TODO: check if there are ip options
    // this is the constraint we should be looking for:
    /*
      (Eq (w32 0)
        (Or w32 (ZExt w32 (Eq (w32 0)
          (Or w32 (ZExt w32 (Eq (w8 6) N0:(Read w8 (w32 50) packet_chunks)))
            (ZExt w32 (Eq (w8 17) N0)))))
              (ZExt w32 (Ult (ZExt w64 (Sub w32 (ZExt w32 (ReadLSB w16 (w32 0)
      pkt_len)) (Extract w32 0 (Add w64 (w64 34) (ZExt w64 (Extract w16 0
      (Extract w32 0 (Mul w64 (w64 4) (SExt w64 (Extract w32 0 (Add w64 (w64
      18446744073709551611) (SExt w64 (ZExt w32 (Extract w8 0 (And w32 (ZExt w32
      (Read w8 (w32 41) packet_chunks)) (w32 15))))))))))))))))
    */

    // we are just going to assume that if the requested length is not constant,
    // then this is a request for ip options

    return len->getKind() != klee::Expr::Kind::Constant;
  }

  processing_result_t process_call(const ExecutionPlan &ep,
                                   BDD::BDDNode_ptr node,
                                   const BDD::Call *casted) override {
    processing_result_t result;
    auto call = casted->get_call();

    if (call.function_name != "packet_borrow_next_chunk") {
      return result;
    }

    // IP options should come after IPv4Consume
    auto all_prev_packet_borrow_next_chunk =
        get_all_prev_functions(casted, "packet_borrow_next_chunk");

    if (all_prev_packet_borrow_next_chunk.size() < 2) {
      return result;
    }

    assert(!call.args["length"].expr.isNull());
    assert(!call.extra_vars["the_chunk"].second.isNull());

    auto _length = call.args["length"].expr;
    auto _chunk = call.extra_vars["the_chunk"].second;

    auto valid = is_valid_ipv4(all_prev_packet_borrow_next_chunk[1].get(),
                               node->get_constraints());

    valid &= is_valid_ip_options(all_prev_packet_borrow_next_chunk[0].get(),
                                 _length, node->get_constraints());

    if (!valid) {
      return result;
    }

    auto new_module = std::make_shared<IPOptionsConsume>(node, _chunk, _length);
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
    auto cloned = new IPOptionsConsume(node, chunk, length);
    return std::shared_ptr<Module>(cloned);
  }

  virtual bool equals(const Module *other) const override {
    if (other->get_type() != type) {
      return false;
    }

    auto other_cast = static_cast<const IPOptionsConsume *>(other);

    if (!BDD::solver_toolbox.are_exprs_always_equal(chunk, other_cast->chunk)) {
      return false;
    }

    if (!BDD::solver_toolbox.are_exprs_always_equal(length,
                                                    other_cast->length)) {
      return false;
    }

    return true;
  }

  const klee::ref<klee::Expr> &get_chunk() const { return chunk; }
  const klee::ref<klee::Expr> &get_length() const { return length; }
};
} // namespace BMv2SimpleSwitchgRPC
} // namespace targets
} // namespace synapse
