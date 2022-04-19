#include "klee_expr_to_p4.h"

namespace synapse {

bool is_bool(klee::ref<klee::Expr> expr) {
  assert(!expr.isNull());

  if (expr->getKind() == klee::Expr::ZExt ||
      expr->getKind() == klee::Expr::SExt) {
    return is_bool(expr->getKid(0));
  }

  return expr->getKind() == klee::Expr::Eq ||
         expr->getKind() == klee::Expr::Not ||
         expr->getKind() == klee::Expr::Or ||
         expr->getKind() == klee::Expr::And ||
         expr->getKind() == klee::Expr::Uge ||
         expr->getKind() == klee::Expr::Ugt ||
         expr->getKind() == klee::Expr::Ule ||
         expr->getKind() == klee::Expr::Ult ||
         expr->getKind() == klee::Expr::Sge ||
         expr->getKind() == klee::Expr::Sgt ||
         expr->getKind() == klee::Expr::Sle ||
         expr->getKind() == klee::Expr::Slt;
}

bool KleeExprToP4::is_read_lsb(klee::ref<klee::Expr> e) const {
  RetrieveSymbols retriever;
  retriever.visit(e);

  if (retriever.get_retrieved_strings().size() != 1) {
    return false;
  }

  auto sz = e->getWidth();
  assert(sz % 8 == 0);

  std::pair<bool, uint64_t> index;

  if (e->getKind() != klee::Expr::Kind::Concat) {
    return false;
  }

  while (e->getKind() == klee::Expr::Kind::Concat) {
    auto msb = e->getKid(0);
    auto lsb = e->getKid(1);

    if (msb->getKind() != klee::Expr::Kind::Read) {
      return false;
    }

    auto msb_index = msb->getKid(0);

    if (msb_index->getKind() != klee::Expr::Kind::Constant) {
      return false;
    }

    auto const_msb_index = static_cast<klee::ConstantExpr *>(msb_index.get());

    if (!index.first) {
      index.first = true;
      index.second = const_msb_index->getZExtValue();
    }

    if (index.first && const_msb_index->getZExtValue() != index.second) {
      return false;
    }

    index.second--;
    e = lsb;
  }

  if (e->getKind() == klee::Expr::Kind::Read) {
    auto last_index = e->getKid(0);

    if (last_index->getKind() != klee::Expr::Kind::Constant) {
      return false;
    }

    auto const_last_index = static_cast<klee::ConstantExpr *>(last_index.get());

    if (const_last_index->getZExtValue() != index.second) {
      return false;
    }
  }

  return true;
}

klee::ExprVisitor::Action KleeExprToP4::visitRead(const klee::ReadExpr &e) {
  klee::ref<klee::Expr> eref = const_cast<klee::ReadExpr *>(&e);

  RetrieveSymbols retriever;
  retriever.visit(eref);

  auto symbols = retriever.get_retrieved_strings();
  assert(symbols.size());
  auto symbol = *symbols.begin();

  if (symbol == "packet_chunks") {
    auto label = generator.label_from_packet_chunk(eref);

    if (label.size() == 0) {
      generator.err_label_from_chunk(eref);
    }

    code << label;
    return klee::ExprVisitor::Action::skipChildren();
  }

  auto label = generator.label_from_vars(eref);

  if (label.size() == 0) {
    generator.err_label_from_vars(eref);
  }

  code << label;
  return klee::ExprVisitor::Action::skipChildren();
}

void KleeExprToP4::swap_endianness(klee::ref<klee::Expr> &expr) {
  RetrieveSymbols retriever;
  retriever.visit(expr);
  auto symbols = retriever.get_retrieved_strings();

  if (symbols.size() != 1 || *symbols.begin() != "packet_chunks") {
    return;
  }

  BDD::SwapPacketEndianness swapper;
  auto after = swapper.swap(expr);

  expr = after;
}

klee::ExprVisitor::Action KleeExprToP4::visitSelect(const klee::SelectExpr &e) {
  e.dump();
  std::cerr << "\n";
  assert(false && "TODO");
  return klee::ExprVisitor::Action::skipChildren();
}

klee::ExprVisitor::Action KleeExprToP4::visitConcat(const klee::ConcatExpr &e) {
  klee::ref<klee::Expr> eref = const_cast<klee::ConcatExpr *>(&e);

  if (is_read_lsb(eref)) {
    RetrieveSymbols retriever;
    retriever.visit(eref);

    auto symbols = retriever.get_retrieved_strings();
    assert(symbols.size() == 1);
    auto symbol = *symbols.begin();

    if (symbol == "VIGOR_DEVICE") {
      code << "standard_metadata.ingress_port";
      return klee::ExprVisitor::Action::skipChildren();
    }

    if (symbol == "pkt_len") {
      code << "standard_metadata.packet_length";
      return klee::ExprVisitor::Action::skipChildren();
    }

    if (symbol == "packet_chunks") {
      auto label = generator.label_from_packet_chunk(eref);

      if (label.size() == 0) {
        generator.err_label_from_chunk(eref);
      }

      code << label;
      return klee::ExprVisitor::Action::skipChildren();
    }

    auto label = generator.label_from_vars(eref);

    if (label.size() == 0) {
      generator.err_label_from_vars(eref);
    }

    code << label;
    return klee::ExprVisitor::Action::skipChildren();
  }

  std::cerr << "expr: " << expr_to_string(eref, true) << "\n";

  assert(false && "TODO");
  return klee::ExprVisitor::Action::skipChildren();
}

klee::ExprVisitor::Action
KleeExprToP4::visitExtract(const klee::ExtractExpr &e) {
  auto sz = e.getWidth();
  auto expr = e.expr;
  auto offset = e.offset;

  auto eref = const_cast<klee::ExtractExpr *>(&e);

  // check if there is a match in headers
  auto chunk = generator.label_from_packet_chunk(expr);

  if (chunk.size()) {
    code << chunk;
    return klee::ExprVisitor::Action::skipChildren();
  }

  while (expr->getKind() == klee::Expr::Kind::Concat) {
    auto msb = expr->getKid(0);
    auto lsb = expr->getKid(1);

    auto msb_sz = msb->getWidth();
    auto lsb_sz = lsb->getWidth();

    if (offset + sz == lsb_sz && offset == 0) {
      expr = lsb;
      break;
    }

    if (offset + sz <= lsb_sz) {
      expr = lsb;
    }

    else if (offset >= lsb_sz) {
      offset -= lsb_sz;
      assert(offset + sz <= msb_sz);
      expr = msb;
    }

    else {
      assert(false);
    }
  }

  if (offset == 0 && expr->getWidth() == sz) {
    code << generator.transpile(expr, is_signed);
    return klee::ExprVisitor::Action::skipChildren();
  }

  if (expr->getKind() == klee::Expr::Constant && sz <= 64) {
    auto extracted = BDD::solver_toolbox.exprBuilder->Extract(expr, offset, sz);
    auto value = BDD::solver_toolbox.value_from_expr(extracted);

    code << value;

    return klee::ExprVisitor::Action::skipChildren();
  }

  if (sz <= 64) {
    uint64_t mask = 0;
    for (unsigned b = 0; b < sz; b++) {
      mask <<= 1;
      mask |= 1;
    }

    assert(mask > 0);
    if (offset > 0) {
      code << "(";
    }

    code << "(";
    code << "(" << generator.p4_type_from_expr(eref) << ") ";
    code << "(" << generator.transpile(expr, is_signed) << ")";
    code << ")";

    if (offset > 0) {
      code << " >> " << offset << ")";
    }

    code << " & ";

    code << "(";
    code << "(" << generator.p4_type_from_expr(eref) << ") ";

    code << std::hex;
    code << "0x" << mask;
    code << std::dec;
    code << ")";

    return klee::ExprVisitor::Action::skipChildren();
  }

  e.dump();
  std::cerr << "\n";

  assert(false && "TODO");
  return klee::ExprVisitor::Action::skipChildren();
}

klee::ExprVisitor::Action KleeExprToP4::visitZExt(const klee::ZExtExpr &e) {
  auto sz = e.getWidth();
  auto expr = e.getKid(0);
  assert(sz % 8 == 0);

  klee::ref<klee::Expr> eref = const_cast<klee::ZExtExpr *>(&e);

  if (is_bool(eref)) {
    code << generator.transpile(expr, is_signed);
  } else {
    code << "(";
    code << "(" << generator.p4_type_from_expr(eref) << ") ";
    code << "(" << generator.transpile(expr, is_signed) << ")";
    code << ")";
  }

  return klee::ExprVisitor::Action::skipChildren();
}

klee::ExprVisitor::Action KleeExprToP4::visitSExt(const klee::SExtExpr &e) {
  auto sz = e.getWidth();
  auto expr = e.getKid(0);
  assert(sz % 8 == 0);

  klee::ref<klee::Expr> eref = const_cast<klee::SExtExpr *>(&e);

  if (is_bool(eref)) {
    is_signed = true;
    code << generator.transpile(expr, is_signed);
  } else {
    code << "(";
    code << "(" << generator.p4_type_from_expr(eref) << ") ";
    code << "(" << generator.transpile(expr, true) << ")";
    code << ")";
  }

  return klee::ExprVisitor::Action::skipChildren();
}

klee::ExprVisitor::Action KleeExprToP4::visitAdd(const klee::AddExpr &e) {
  assert(e.getNumKids() == 2);

  auto lhs = e.getKid(0);
  auto rhs = e.getKid(1);

  if (generator.is_constant_signed(lhs) && !generator.is_constant_signed(rhs)) {
    auto constant_signed = generator.get_constant_signed(lhs);
    auto rhs_parsed = generator.transpile(rhs, is_signed);

    code << "(" << rhs_parsed << ") ";
    code << constant_signed;
  }

  else if (generator.is_constant_signed(rhs) &&
           !generator.is_constant_signed(lhs)) {
    auto constant_signed = generator.get_constant_signed(rhs);
    auto lhs_parsed = generator.transpile(lhs, is_signed);

    code << "(" << lhs_parsed << ") ";
    code << constant_signed;
  }

  else {
    auto lhs_parsed = generator.transpile(lhs, is_signed);
    auto rhs_parsed = generator.transpile(rhs, is_signed);

    code << "(" << lhs_parsed << ")";
    code << " + ";
    code << "(" << rhs_parsed << ")";
  }

  return klee::ExprVisitor::Action::skipChildren();
}

klee::ExprVisitor::Action KleeExprToP4::visitSub(const klee::SubExpr &e) {
  assert(e.getNumKids() == 2);

  auto lhs = e.getKid(0);
  auto rhs = e.getKid(1);

  auto lhs_parsed = generator.transpile(lhs, is_signed);
  auto rhs_parsed = generator.transpile(rhs, is_signed);

  code << "(";
  code << "(" << generator.p4_type_from_expr(lhs) << ")";
  code << "(" << lhs_parsed << ")";
  code << ")";

  code << " - ";

  code << "(";
  code << "(" << generator.p4_type_from_expr(rhs) << ")";
  code << "(" << rhs_parsed << ")";
  code << ")";

  return klee::ExprVisitor::Action::skipChildren();
}

klee::ExprVisitor::Action KleeExprToP4::visitMul(const klee::MulExpr &e) {
  assert(e.getNumKids() == 2);

  auto lhs = e.getKid(0);
  auto rhs = e.getKid(1);

  auto lhs_parsed = generator.transpile(lhs, is_signed);
  auto rhs_parsed = generator.transpile(rhs, is_signed);

  code << "(";
  code << "(" << generator.p4_type_from_expr(lhs) << ")";
  code << "(" << lhs_parsed << ")";
  code << ")";

  code << " * ";

  code << "(";
  code << "(" << generator.p4_type_from_expr(rhs) << ")";
  code << "(" << rhs_parsed << ")";
  code << ")";

  return klee::ExprVisitor::Action::skipChildren();
}

klee::ExprVisitor::Action KleeExprToP4::visitUDiv(const klee::UDivExpr &e) {
  assert(false && "No division in bmv2!");
  return klee::ExprVisitor::Action::skipChildren();
}

klee::ExprVisitor::Action KleeExprToP4::visitSDiv(const klee::SDivExpr &e) {
  assert(false && "No division in bmv2!");
  return klee::ExprVisitor::Action::skipChildren();
}

klee::ExprVisitor::Action KleeExprToP4::visitURem(const klee::URemExpr &e) {
  assert(false && "No division in bmv2!");
  return klee::ExprVisitor::Action::skipChildren();
}

klee::ExprVisitor::Action KleeExprToP4::visitSRem(const klee::SRemExpr &e) {
  assert(false && "No division in bmv2!");
  return klee::ExprVisitor::Action::skipChildren();
}

klee::ExprVisitor::Action KleeExprToP4::visitNot(const klee::NotExpr &e) {
  assert(e.getNumKids() == 1);

  auto arg = e.getKid(0);
  auto arg_parsed = generator.transpile(arg, is_signed);
  code << "!" << arg_parsed;

  return klee::ExprVisitor::Action::skipChildren();
}

klee::ExprVisitor::Action KleeExprToP4::visitAnd(const klee::AndExpr &e) {
  assert(e.getNumKids() == 2);

  auto lhs = e.getKid(0);
  auto rhs = e.getKid(1);

  auto lhs_parsed = generator.transpile(lhs, is_signed);
  auto rhs_parsed = generator.transpile(rhs, is_signed);

  auto isBool = is_bool(lhs) || is_bool(rhs);

  if (isBool) {
    code << "(" << lhs_parsed << ")";
    code << " && ";
    code << "(" << rhs_parsed << ")";
  } else {
    code << "(";
    code << "(" << generator.p4_type_from_expr(lhs) << ")";
    code << "(" << lhs_parsed << ")";
    code << ")";

    code << " & ";

    code << "(";
    code << "(" << generator.p4_type_from_expr(rhs) << ")";
    code << "(" << rhs_parsed << ")";
    code << ")";
  }

  return klee::ExprVisitor::Action::skipChildren();
}

klee::ExprVisitor::Action KleeExprToP4::visitOr(const klee::OrExpr &e) {
  assert(e.getNumKids() == 2);

  auto lhs = e.getKid(0);
  auto rhs = e.getKid(1);

  auto lhs_parsed = generator.transpile(lhs, is_signed);
  auto rhs_parsed = generator.transpile(rhs, is_signed);

  auto isBool = is_bool(lhs) || is_bool(rhs);

  if (isBool) {
    code << "(" << lhs_parsed << ")";
    code << " || ";
    code << "(" << rhs_parsed << ")";
  } else {
    code << "(";
    code << "(" << generator.p4_type_from_expr(lhs) << ")";
    code << "(" << lhs_parsed << ")";
    code << ")";

    code << " | ";

    code << "(";
    code << "(" << generator.p4_type_from_expr(rhs) << ")";
    code << "(" << rhs_parsed << ")";
    code << ")";
  }

  return klee::ExprVisitor::Action::skipChildren();
}

klee::ExprVisitor::Action KleeExprToP4::visitXor(const klee::XorExpr &e) {
  assert(e.getNumKids() == 2);

  auto lhs = e.getKid(0);
  auto rhs = e.getKid(1);

  auto lhs_parsed = generator.transpile(lhs, is_signed);
  auto rhs_parsed = generator.transpile(rhs, is_signed);

  code << "(";
  code << "(" << generator.p4_type_from_expr(lhs) << ")";
  code << "(" << lhs_parsed << ")";
  code << ")";

  code << " ^ ";

  code << "(";
  code << "(" << generator.p4_type_from_expr(rhs) << ")";
  code << "(" << rhs_parsed << ")";
  code << ")";

  return klee::ExprVisitor::Action::skipChildren();
}

klee::ref<klee::Expr> change_width(klee::ref<klee::Expr> expr,
                                   klee::Expr::Width width) {
  auto modified = expr;

  if (expr->getWidth() == width) {
    return expr;
  }

  if (expr->getWidth() > width) {
    modified = BDD::solver_toolbox.exprBuilder->Extract(expr, 0, width);

    auto eq = BDD::solver_toolbox.are_exprs_always_equal(
        expr,
        BDD::solver_toolbox.exprBuilder->ZExt(modified, expr->getWidth()));

    assert(eq);
  } else {
    modified = BDD::solver_toolbox.exprBuilder->ZExt(expr, width);

    auto eq = BDD::solver_toolbox.are_exprs_always_equal(
        expr, BDD::solver_toolbox.exprBuilder->Extract(modified, 0,
                                                       expr->getWidth()));

    assert(eq);
  }

  return modified;
}

klee::ExprVisitor::Action KleeExprToP4::visitShl(const klee::ShlExpr &e) {
  assert(e.getNumKids() == 2);

  auto lhs = e.getKid(0);
  auto rhs = e.getKid(1);

  auto lhs_parsed = generator.transpile(lhs, is_signed);

  code << "(";
  code << "(" << generator.p4_type_from_expr(lhs) << ")";
  code << "(" << lhs_parsed << ")";
  code << ")";

  code << " << ";

  auto rhs8 = change_width(rhs, klee::Expr::Int8);
  auto rhs_parsed = generator.transpile(rhs8, false);

  code << "(";
  code << "(" << generator.p4_type_from_expr(rhs8) << ")";
  code << "(" << rhs_parsed << ")";
  code << ")";

  return klee::ExprVisitor::Action::skipChildren();
}

klee::ExprVisitor::Action KleeExprToP4::visitLShr(const klee::LShrExpr &e) {
  assert(e.getNumKids() == 2);

  auto lhs = e.getKid(0);
  auto rhs = e.getKid(1);

  auto lhs_parsed = generator.transpile(lhs, is_signed);

  code << "(";
  code << "(" << generator.p4_type_from_expr(lhs) << ")";
  code << "(" << lhs_parsed << ")";
  code << ")";

  code << " >> ";

  auto rhs8 = change_width(rhs, klee::Expr::Int8);
  auto rhs_parsed = generator.transpile(rhs8, false);

  code << "(";
  code << "(" << generator.p4_type_from_expr(rhs8) << ")";
  code << "(" << rhs_parsed << ")";
  code << ")";

  return klee::ExprVisitor::Action::skipChildren();
}

klee::ExprVisitor::Action KleeExprToP4::visitAShr(const klee::AShrExpr &e) {
  assert(e.getNumKids() == 2);

  auto lhs = e.getKid(0);
  auto rhs = e.getKid(1);

  auto sz = e.getWidth();
  assert(sz % 8 == 0);

  auto lhs_parsed = generator.transpile(lhs, is_signed);
  auto rhs8 = change_width(rhs, klee::Expr::Int8);
  auto rhs_parsed = generator.transpile(rhs8, false);

  std::stringstream sign_bit_stream;
  sign_bit_stream << "(" << lhs_parsed << ")";
  sign_bit_stream << " >> ";
  sign_bit_stream << sz - 1;

  auto sign_bit = sign_bit_stream.str();

  std::stringstream mask_stream;
  mask_stream << "(";
  mask_stream << "(";
  mask_stream << "(" << sign_bit << ")";
  mask_stream << " << ";
  mask_stream << "(" << rhs_parsed << ")";
  mask_stream << ")";
  mask_stream << " - ";
  mask_stream << "(1 & "
              << "(" << sign_bit << ")"
              << ")";
  mask_stream << ")";
  mask_stream << " << ";
  mask_stream << "(" << sz - 1 << " - "
              << "(" << rhs_parsed << ")"
              << ")";

  code << "(";
  code << "(" << lhs_parsed << ")";
  code << " >> ";
  code << "(" << rhs_parsed << ")";
  code << ")";
  code << " | ";
  code << "(" << mask_stream.str() << ")";

  return klee::ExprVisitor::Action::skipChildren();
}

klee::ExprVisitor::Action KleeExprToP4::visitEq(const klee::EqExpr &e) {
  assert(e.getNumKids() == 2);

  auto lhs = e.getKid(0);
  auto rhs = e.getKid(1);

  bool convert_to_bool = false;

  if (rhs->getKind() == klee::Expr::Concat && is_read_lsb(rhs)) {
    RetrieveSymbols retriever;
    retriever.visit(rhs);

    auto symbols = retriever.get_retrieved_strings();
    assert(symbols.size() == 1);
    auto symbol = *symbols.begin();

    for (auto local_var : generator.local_vars.get()) {
      auto local_var_vigor_symbol = local_var.symbol;

      if (symbol == local_var_vigor_symbol) {
        convert_to_bool = true;
      }
    }
  }

  convert_to_bool |= (lhs->getWidth() == 1);
  convert_to_bool |= (is_bool(lhs) || is_bool(rhs));

  if (convert_to_bool) {
    if (lhs->getKind() == klee::Expr::Constant) {
      auto constant = static_cast<klee::ConstantExpr *>(lhs.get());
      assert(constant->getWidth() <= 64);
      auto value = constant->getZExtValue();

      code << (value == 0 ? "false" : "true");
    } else {
      assert(false && "TODO");
    }
  }

  else if (generator.transpile(rhs).find("etherType") != std::string::npos) {
    // be careful with endianess
    assert(lhs->getKind() == klee::Expr::Constant);
    auto etherType_val = BDD::solver_toolbox.value_from_expr(lhs);
    auto new_val =
        ((((etherType_val & 0xff) << 8) | ((etherType_val & 0xff00) >> 8)) &
         0xffff);

    code << std::hex;
    code << "0x";
    code << new_val;
    code << std::dec;
  }

  else {
    auto lhs_parsed = generator.transpile(lhs, is_signed);
    code << "(";
    code << "(" << generator.p4_type_from_expr(lhs) << ")";
    code << "(" << lhs_parsed << ")";
    code << ")";
  }

  auto rhs_parsed = generator.transpile(rhs, is_signed);

  code << " == ";

  if (!convert_to_bool) {
    code << "(";
    code << "(" << generator.p4_type_from_expr(rhs) << ")";
    code << "(" << rhs_parsed << ")";
    code << ")";
  } else {
    code << "(" << rhs_parsed << ")";
  }

  return klee::ExprVisitor::Action::skipChildren();
}

klee::ExprVisitor::Action KleeExprToP4::visitNe(const klee::NeExpr &e) {
  assert(e.getNumKids() == 2);

  auto lhs = e.getKid(0);
  auto rhs = e.getKid(1);

  auto lhs_parsed = generator.transpile(lhs, is_signed);
  auto rhs_parsed = generator.transpile(rhs, is_signed);

  code << "(";
  code << "(" << generator.p4_type_from_expr(lhs) << ")";
  code << "(" << lhs_parsed << ")";
  code << ")";

  code << " != ";

  code << "(";
  code << "(" << generator.p4_type_from_expr(rhs) << ")";
  code << "(" << rhs_parsed << ")";
  code << ")";

  return klee::ExprVisitor::Action::skipChildren();
}

klee::ExprVisitor::Action KleeExprToP4::visitUlt(const klee::UltExpr &e) {
  assert(e.getNumKids() == 2);

  auto lhs = e.getKid(0);
  auto rhs = e.getKid(1);

  auto lhs_parsed = generator.transpile(lhs, is_signed);
  auto rhs_parsed = generator.transpile(rhs, is_signed);

  code << "(";
  code << "(" << generator.p4_type_from_expr(lhs) << ")";
  code << "(" << lhs_parsed << ")";
  code << ")";

  code << " < ";

  code << "(";
  code << "(" << generator.p4_type_from_expr(rhs) << ")";
  code << "(" << rhs_parsed << ")";
  code << ")";

  return klee::ExprVisitor::Action::skipChildren();
}

klee::ExprVisitor::Action KleeExprToP4::visitUle(const klee::UleExpr &e) {
  assert(e.getNumKids() == 2);

  auto lhs = e.getKid(0);
  auto rhs = e.getKid(1);

  auto lhs_parsed = generator.transpile(lhs, is_signed);
  auto rhs_parsed = generator.transpile(rhs, is_signed);

  code << "(";
  code << "(" << generator.p4_type_from_expr(lhs) << ")";
  code << "(" << lhs_parsed << ")";
  code << ")";

  code << " <= ";

  code << "(";
  code << "(" << generator.p4_type_from_expr(rhs) << ")";
  code << "(" << rhs_parsed << ")";
  code << ")";

  return klee::ExprVisitor::Action::skipChildren();
}

klee::ExprVisitor::Action KleeExprToP4::visitUgt(const klee::UgtExpr &e) {
  assert(e.getNumKids() == 2);

  auto lhs = e.getKid(0);
  auto rhs = e.getKid(1);

  auto lhs_parsed = generator.transpile(lhs, is_signed);
  auto rhs_parsed = generator.transpile(rhs, is_signed);

  code << "(";
  code << "(" << generator.p4_type_from_expr(lhs) << ")";
  code << "(" << lhs_parsed << ")";
  code << ")";

  code << " > ";

  code << "(";
  code << "(" << generator.p4_type_from_expr(rhs) << ")";
  code << "(" << rhs_parsed << ")";
  code << ")";

  return klee::ExprVisitor::Action::skipChildren();
}

klee::ExprVisitor::Action KleeExprToP4::visitUge(const klee::UgeExpr &e) {
  assert(e.getNumKids() == 2);

  auto lhs = e.getKid(0);
  auto rhs = e.getKid(1);

  auto lhs_parsed = generator.transpile(lhs, is_signed);
  auto rhs_parsed = generator.transpile(rhs, is_signed);

  code << "(";
  code << "(" << generator.p4_type_from_expr(lhs) << ")";
  code << "(" << lhs_parsed << ")";
  code << ")";

  code << " >= ";

  code << "(";
  code << "(" << generator.p4_type_from_expr(rhs) << ")";
  code << "(" << rhs_parsed << ")";
  code << ")";

  return klee::ExprVisitor::Action::skipChildren();
}

klee::ExprVisitor::Action KleeExprToP4::visitSlt(const klee::SltExpr &e) {
  assert(e.getNumKids() == 2);

  auto lhs = e.getKid(0);
  auto rhs = e.getKid(1);

  auto lhs_parsed = generator.transpile(lhs, true);
  auto rhs_parsed = generator.transpile(rhs, true);

  code << "(";
  code << "(" << generator.p4_type_from_expr(lhs) << ")";
  code << "(" << lhs_parsed << ")";
  code << ")";

  code << " < ";

  code << "(";
  code << "(" << generator.p4_type_from_expr(rhs) << ")";
  code << "(" << rhs_parsed << ")";
  code << ")";

  return klee::ExprVisitor::Action::skipChildren();
}

klee::ExprVisitor::Action KleeExprToP4::visitSle(const klee::SleExpr &e) {
  assert(e.getNumKids() == 2);

  is_signed = true;

  auto lhs = e.getKid(0);
  auto rhs = e.getKid(1);

  auto lhs_parsed = generator.transpile(lhs, true);
  auto rhs_parsed = generator.transpile(rhs, true);

  code << "(";
  code << "(" << generator.p4_type_from_expr(lhs) << ")";
  code << "(" << lhs_parsed << ")";
  code << ")";

  code << " <= ";

  code << "(";
  code << "(" << generator.p4_type_from_expr(rhs) << ")";
  code << "(" << rhs_parsed << ")";
  code << ")";

  return klee::ExprVisitor::Action::skipChildren();
}

klee::ExprVisitor::Action KleeExprToP4::visitSgt(const klee::SgtExpr &e) {
  assert(e.getNumKids() == 2);

  is_signed = true;

  auto lhs = e.getKid(0);
  auto rhs = e.getKid(1);

  auto lhs_parsed = generator.transpile(lhs, true);
  auto rhs_parsed = generator.transpile(rhs, true);

  code << "(";
  code << "(" << generator.p4_type_from_expr(lhs) << ")";
  code << "(" << lhs_parsed << ")";
  code << ")";

  code << " > ";

  code << "(";
  code << "(" << generator.p4_type_from_expr(rhs) << ")";
  code << "(" << rhs_parsed << ")";
  code << ")";

  return klee::ExprVisitor::Action::skipChildren();
}

klee::ExprVisitor::Action KleeExprToP4::visitSge(const klee::SgeExpr &e) {
  assert(e.getNumKids() == 2);

  is_signed = true;

  auto lhs = e.getKid(0);
  auto rhs = e.getKid(1);

  auto lhs_parsed = generator.transpile(lhs, true);
  auto rhs_parsed = generator.transpile(rhs, true);

  code << "(";
  code << "(" << generator.p4_type_from_expr(lhs) << ")";
  code << "(" << lhs_parsed << ")";
  code << ")";

  code << " >= ";

  code << "(";
  code << "(" << generator.p4_type_from_expr(rhs) << ")";
  code << "(" << rhs_parsed << ")";
  code << ")";

  return klee::ExprVisitor::Action::skipChildren();
}

} // namespace synapse
