#include "BMv2SimpleSwitchgRPC_generator.h"

namespace synapse {

class KleeExprToP4 : public klee::ExprVisitor::ExprVisitor {
private:
  const BMv2SimpleSwitchgRPC_Generator &generator;
  std::stringstream code;
  bool is_signed;

  bool is_read_lsb(klee::ref<klee::Expr> e) const;

public:
  KleeExprToP4(const BMv2SimpleSwitchgRPC_Generator &_generator,
               bool _is_signed)
      : ExprVisitor(false), generator(_generator), is_signed(_is_signed) {}

  std::string get_code() { return code.str(); }
  static void swap_endianness(klee::ref<klee::Expr> &expr);

  klee::ExprVisitor::Action visitRead(const klee::ReadExpr &);
  klee::ExprVisitor::Action visitSelect(const klee::SelectExpr &);
  klee::ExprVisitor::Action visitConcat(const klee::ConcatExpr &);
  klee::ExprVisitor::Action visitExtract(const klee::ExtractExpr &);
  klee::ExprVisitor::Action visitZExt(const klee::ZExtExpr &);
  klee::ExprVisitor::Action visitSExt(const klee::SExtExpr &);
  klee::ExprVisitor::Action visitAdd(const klee::AddExpr &);
  klee::ExprVisitor::Action visitSub(const klee::SubExpr &);
  klee::ExprVisitor::Action visitMul(const klee::MulExpr &);
  klee::ExprVisitor::Action visitUDiv(const klee::UDivExpr &);
  klee::ExprVisitor::Action visitSDiv(const klee::SDivExpr &);
  klee::ExprVisitor::Action visitURem(const klee::URemExpr &);
  klee::ExprVisitor::Action visitSRem(const klee::SRemExpr &);
  klee::ExprVisitor::Action visitNot(const klee::NotExpr &);
  klee::ExprVisitor::Action visitAnd(const klee::AndExpr &);
  klee::ExprVisitor::Action visitOr(const klee::OrExpr &);
  klee::ExprVisitor::Action visitXor(const klee::XorExpr &);
  klee::ExprVisitor::Action visitShl(const klee::ShlExpr &);
  klee::ExprVisitor::Action visitLShr(const klee::LShrExpr &);
  klee::ExprVisitor::Action visitAShr(const klee::AShrExpr &);
  klee::ExprVisitor::Action visitEq(const klee::EqExpr &);
  klee::ExprVisitor::Action visitNe(const klee::NeExpr &);
  klee::ExprVisitor::Action visitUlt(const klee::UltExpr &);
  klee::ExprVisitor::Action visitUle(const klee::UleExpr &);
  klee::ExprVisitor::Action visitUgt(const klee::UgtExpr &);
  klee::ExprVisitor::Action visitUge(const klee::UgeExpr &);
  klee::ExprVisitor::Action visitSlt(const klee::SltExpr &);
  klee::ExprVisitor::Action visitSle(const klee::SleExpr &);
  klee::ExprVisitor::Action visitSgt(const klee::SgtExpr &);
  klee::ExprVisitor::Action visitSge(const klee::SgeExpr &);
};
} // namespace synapse
