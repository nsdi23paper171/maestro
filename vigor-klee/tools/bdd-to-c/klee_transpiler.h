#pragma once

#include <algorithm>
#include <dlfcn.h>
#include <expr/Parser.h>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <memory>
#include <regex>
#include <stack>
#include <vector>

#include "klee/ExprBuilder.h"
#include "klee/perf-contracts.h"
#include "klee/util/ArrayCache.h"
#include "klee/util/ExprSMTLIBPrinter.h"
#include "klee/util/ExprVisitor.h"
#include "llvm/Support/CommandLine.h"
#include <klee/Constraints.h>
#include <klee/Solver.h>

#include "call-paths-to-bdd.h"

#include "ast.h"
#include "nodes.h"

class AST;

Expr_ptr transpile(AST *ast, const klee::ref<klee::Expr> &e,
                   bool pointer_to_int = false);
std::vector<Expr_ptr> apply_changes_to_match(AST *ast,
                                             const klee::ref<klee::Expr> &e1,
                                             const klee::ref<klee::Expr> &e2);
std::vector<Expr_ptr> apply_changes(AST *ast, Expr_ptr variable,
                                    klee::ref<klee::Expr> before,
                                    klee::ref<klee::Expr> after);
std::vector<Expr_ptr> build_and_fill_byte_array(AST *ast, Expr_ptr var,
                                                klee::ref<klee::Expr> expr);
Constant_ptr const_to_ast_expr(const klee::ref<klee::Expr> &e);
uint64_t get_first_concat_idx(const klee::ref<klee::Expr> &e);
uint64_t get_last_concat_idx(const klee::ref<klee::Expr> &e);
Type_ptr type_from_klee_expr(klee::ref<klee::Expr> expr, bool force_byte_array);

class KleeExprToASTNodeConverter : public klee::ExprVisitor::ExprVisitor {
private:
  AST *ast;
  Expr_ptr result;
  std::pair<bool, unsigned int> symbol_width;
  bool pointer_to_int;

  void save_result(Expr_ptr _result) { result = _result->clone(); }

public:
  KleeExprToASTNodeConverter(AST *_ast)
      : ExprVisitor(false), ast(_ast), pointer_to_int(false) {}
  KleeExprToASTNodeConverter(AST *_ast, bool _pointer_to_int)
      : ExprVisitor(false), ast(_ast), pointer_to_int(_pointer_to_int) {}

  std::pair<bool, unsigned int> get_symbol_width() const {
    return symbol_width;
  }

  Expr_ptr get_result() {
    return (result == nullptr ? result : result->clone());
  }

  klee::ExprVisitor::Action visitRead(const klee::ReadExpr &e);
  klee::ExprVisitor::Action visitSelect(const klee::SelectExpr &e);
  klee::ExprVisitor::Action visitConcat(const klee::ConcatExpr &e);
  klee::ExprVisitor::Action visitExtract(const klee::ExtractExpr &e);
  klee::ExprVisitor::Action visitZExt(const klee::ZExtExpr &e);
  klee::ExprVisitor::Action visitSExt(const klee::SExtExpr &e);
  klee::ExprVisitor::Action visitAdd(const klee::AddExpr &e);
  klee::ExprVisitor::Action visitSub(const klee::SubExpr &e);
  klee::ExprVisitor::Action visitMul(const klee::MulExpr &e);
  klee::ExprVisitor::Action visitUDiv(const klee::UDivExpr &e);
  klee::ExprVisitor::Action visitSDiv(const klee::SDivExpr &e);
  klee::ExprVisitor::Action visitURem(const klee::URemExpr &e);
  klee::ExprVisitor::Action visitSRem(const klee::SRemExpr &e);
  klee::ExprVisitor::Action visitNot(const klee::NotExpr &e);
  klee::ExprVisitor::Action visitAnd(const klee::AndExpr &e);
  klee::ExprVisitor::Action visitOr(const klee::OrExpr &e);
  klee::ExprVisitor::Action visitXor(const klee::XorExpr &e);
  klee::ExprVisitor::Action visitShl(const klee::ShlExpr &e);
  klee::ExprVisitor::Action visitLShr(const klee::LShrExpr &e);
  klee::ExprVisitor::Action visitAShr(const klee::AShrExpr &e);
  klee::ExprVisitor::Action visitEq(const klee::EqExpr &e);
  klee::ExprVisitor::Action visitNe(const klee::NeExpr &e);
  klee::ExprVisitor::Action visitUlt(const klee::UltExpr &e);
  klee::ExprVisitor::Action visitUle(const klee::UleExpr &e);
  klee::ExprVisitor::Action visitUgt(const klee::UgtExpr &e);
  klee::ExprVisitor::Action visitUge(const klee::UgeExpr &e);
  klee::ExprVisitor::Action visitSlt(const klee::SltExpr &e);
  klee::ExprVisitor::Action visitSle(const klee::SleExpr &e);
  klee::ExprVisitor::Action visitSgt(const klee::SgtExpr &e);
  klee::ExprVisitor::Action visitSge(const klee::SgeExpr &e);
};
