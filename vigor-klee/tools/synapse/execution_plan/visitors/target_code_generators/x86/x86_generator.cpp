#include "x86_generator.h"
#include "../../../../log.h"
#include "../../../../modules/BMv2SimpleSwitchgRPC/table_lookup.h"
#include "../../../../modules/x86/x86.h"

#define MARKER_GLOBAL_STATE "global_state"
#define MARKER_RUNTIME_CONFIGURE "runtime_configure"
#define MARKER_NF_INIT "nf_init"
#define MARKER_NF_PROCESS "nf_process"

namespace synapse {

std::string build_table_label(std::string bdd_function, uint64_t table_id) {
  std::stringstream table_label;

  table_label << bdd_function;
  table_label << "_";
  table_label << table_id;

  return table_label.str();
}

std::string build_table_name(std::string bdd_function, uint64_t table_id) {
  std::stringstream table_name;

  table_name << "SyNAPSE_Ingress.";
  table_name << build_table_label(bdd_function, table_id);

  return table_name.str();
}

std::string get_label(BDD::symbols_t symbols, std::string base) {
  auto found_it = std::find_if(
      symbols.begin(), symbols.end(),
      [&](const BDD::symbol_t &symbol) { return symbol.label_base == base; });

  assert(found_it != symbols.end());
  return found_it->label;
}

std::string transpile(const klee::ref<klee::Expr> &e, stack_t &stack,
                      bool is_signed);
std::string transpile(const klee::ref<klee::Expr> &e, stack_t &stack);

class KleeExprToC : public klee::ExprVisitor::ExprVisitor {
private:
  std::stringstream code;
  stack_t &stack;
  bool is_signed;

  bool is_read_lsb(klee::ref<klee::Expr> e) const {
    RetrieveSymbols retriever;
    retriever.visit(e);

    if (retriever.get_retrieved_strings().size() != 1) {
      return false;
    }

    auto sz = e->getWidth();
    assert(sz % 8 == 0);
    auto index = (sz / 8) - 1;

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

      if (const_msb_index->getZExtValue() != index) {
        return false;
      }

      index--;
      e = lsb;
    }

    if (e->getKind() == klee::Expr::Kind::Read) {
      auto last_index = e->getKid(0);

      if (last_index->getKind() != klee::Expr::Kind::Constant) {
        return false;
      }

      auto const_last_index =
          static_cast<klee::ConstantExpr *>(last_index.get());

      if (const_last_index->getZExtValue() != index) {
        return false;
      }
    }

    return index == 0;
  }

public:
  KleeExprToC(stack_t &_stack)
      : ExprVisitor(false), stack(_stack), is_signed(false) {}
  KleeExprToC(stack_t &_stack, bool _is_signed)
      : ExprVisitor(false), stack(_stack), is_signed(_is_signed) {}

  std::string get_code() { return code.str(); }

  klee::ExprVisitor::Action visitRead(const klee::ReadExpr &e) {
    klee::ref<klee::Expr> eref = const_cast<klee::ReadExpr *>(&e);

    RetrieveSymbols retriever;
    retriever.visit(eref);

    auto symbols = retriever.get_retrieved_strings();
    assert(symbols.size() == 1);
    auto symbol = *symbols.begin();

    if (stack.has_label(symbol)) {
      auto value = stack.get_value(symbol);
      auto offset = e.index;

      assert(offset->getKind() == klee::Expr::Kind::Constant);
      auto offset_value = BDD::solver_toolbox.value_from_expr(offset);

      auto extracted = BDD::solver_toolbox.exprBuilder->Extract(
          value, offset_value, eref->getWidth());
      code << transpile(extracted, stack, is_signed);

      return klee::ExprVisitor::Action::skipChildren();
    }

    e.dump();
    std::cerr << "\n";
    assert(false && "TODO");
    return klee::ExprVisitor::Action::skipChildren();
  }

  klee::ExprVisitor::Action visitSelect(const klee::SelectExpr &e) {
    e.dump();
    std::cerr << "\n";
    assert(false && "TODO");
    return klee::ExprVisitor::Action::skipChildren();
  }

  klee::ExprVisitor::Action visitConcat(const klee::ConcatExpr &e) {
    klee::ref<klee::Expr> eref = const_cast<klee::ConcatExpr *>(&e);

    if (is_read_lsb(eref)) {
      RetrieveSymbols retriever;
      retriever.visit(eref);

      auto symbols = retriever.get_retrieved_strings();
      assert(symbols.size() == 1);
      auto symbol = *symbols.begin();

      if (stack.has_label(symbol)) {
        code << symbol;
        return klee::ExprVisitor::Action::skipChildren();
      }

      klee::ref<klee::Expr> eref = const_cast<klee::ConcatExpr *>(&e);

      Log::err() << "\n";
      Log::err() << expr_to_string(eref, true) << "\n";
      Log::err() << "symbol " << symbol << " not in set\n";
      stack.err_dump();
      assert(false && "Not in stack");
    }

    e.dump();
    std::cerr << "\n";
    assert(false && "TODO");
    return klee::ExprVisitor::Action::skipChildren();
  }

  klee::ExprVisitor::Action visitExtract(const klee::ExtractExpr &e) {
    auto expr = e.expr;
    auto offset = e.offset;
    auto sz = e.width;

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
      code << transpile(expr, stack, is_signed);
      return klee::ExprVisitor::Action::skipChildren();
    }

    if (expr->getWidth() <= 64) {
      uint64_t mask = 0;
      for (unsigned b = 0; b < expr->getWidth(); b++) {
        mask <<= 1;
        mask |= 1;
      }

      assert(mask > 0);
      if (offset > 0) {
        code << "(";
      }

      code << transpile(expr, stack, is_signed);

      if (offset > 0) {
        code << " >> " << offset << ")";
      }

      code << " & 0x";
      code << std::hex;
      code << mask;
      code << std::dec;

      return klee::ExprVisitor::Action::skipChildren();
    }

    if (expr->getKind() == klee::Expr::Kind::Constant) {
      auto extract = BDD::solver_toolbox.exprBuilder->Extract(expr, offset, sz);
      auto value = BDD::solver_toolbox.value_from_expr(extract);

      // checking if this is really the ONLY answer
      assert(BDD::solver_toolbox.are_exprs_always_equal(
          extract, BDD::solver_toolbox.exprBuilder->Constant(value, sz)));

      code << value;
      return klee::ExprVisitor::Action::skipChildren();
    }

    std::cerr << "expr   " << expr_to_string(expr, true) << "\n";
    std::cerr << "offset " << offset << "\n";
    std::cerr << "sz     " << sz << "\n";
    assert(false && "expr size > 64 but not constant");

    return klee::ExprVisitor::Action::skipChildren();
  }

  klee::ExprVisitor::Action visitZExt(const klee::ZExtExpr &e) {
    auto sz = e.getWidth();
    auto expr = e.getKid(0);
    assert(sz % 8 == 0);

    code << "(";

    switch (sz) {
    case klee::Expr::Int8:
      code << "uint8_t";
      break;
    case klee::Expr::Int16:
      code << "uint16_t";
      break;
    case klee::Expr::Int32:
      code << "uint32_t";
      break;
    case klee::Expr::Int64:
      code << "uint64_t";
      break;
    default:
      assert(false);
    }

    code << ")";
    code << "(";
    code << transpile(expr, stack, is_signed);
    code << ")";

    return klee::ExprVisitor::Action::skipChildren();
  }

  klee::ExprVisitor::Action visitSExt(const klee::SExtExpr &e) {
    auto sz = e.getWidth();
    auto expr = e.getKid(0);
    assert(sz % 8 == 0);

    code << "(";

    switch (sz) {
    case klee::Expr::Int8:
      code << "int8_t";
      break;
    case klee::Expr::Int16:
      code << "int16_t";
      break;
    case klee::Expr::Int32:
      code << "int32_t";
      break;
    case klee::Expr::Int64:
      code << "int64_t";
      break;
    default:
      assert(false);
    }

    code << ")";
    code << "(";
    code << transpile(expr, stack, is_signed);
    code << ")";

    return klee::ExprVisitor::Action::skipChildren();
  }

  klee::ExprVisitor::Action visitAdd(const klee::AddExpr &e) {
    assert(e.getNumKids() == 2);

    auto lhs = e.getKid(0);
    auto rhs = e.getKid(1);

    auto lhs_parsed = transpile(lhs, stack, is_signed);
    auto rhs_parsed = transpile(rhs, stack, is_signed);

    code << "(" << lhs_parsed << ")";
    code << " + ";
    code << "(" << rhs_parsed << ")";

    return klee::ExprVisitor::Action::skipChildren();
  }

  klee::ExprVisitor::Action visitSub(const klee::SubExpr &e) {
    assert(e.getNumKids() == 2);

    auto lhs = e.getKid(0);
    auto rhs = e.getKid(1);

    auto lhs_parsed = transpile(lhs, stack, is_signed);
    auto rhs_parsed = transpile(rhs, stack, is_signed);

    code << "(" << lhs_parsed << ")";
    code << " - ";
    code << "(" << rhs_parsed << ")";

    return klee::ExprVisitor::Action::skipChildren();
  }

  klee::ExprVisitor::Action visitMul(const klee::MulExpr &e) {
    assert(e.getNumKids() == 2);

    auto lhs = e.getKid(0);
    auto rhs = e.getKid(1);

    auto lhs_parsed = transpile(lhs, stack, is_signed);
    auto rhs_parsed = transpile(rhs, stack, is_signed);

    code << "(" << lhs_parsed << ")";
    code << " * ";
    code << "(" << rhs_parsed << ")";

    return klee::ExprVisitor::Action::skipChildren();
  }

  klee::ExprVisitor::Action visitUDiv(const klee::UDivExpr &e) {
    assert(e.getNumKids() == 2);

    auto lhs = e.getKid(0);
    auto rhs = e.getKid(1);

    auto lhs_parsed = transpile(lhs, stack, is_signed);
    auto rhs_parsed = transpile(rhs, stack, is_signed);

    code << "(" << lhs_parsed << ")";
    code << " / ";
    code << "(" << rhs_parsed << ")";

    return klee::ExprVisitor::Action::skipChildren();
  }

  klee::ExprVisitor::Action visitSDiv(const klee::SDivExpr &e) {
    assert(e.getNumKids() == 2);

    auto lhs = e.getKid(0);
    auto rhs = e.getKid(1);

    auto lhs_parsed = transpile(lhs, stack, true);
    auto rhs_parsed = transpile(rhs, stack, true);

    code << "(" << lhs_parsed << ")";
    code << " / ";
    code << "(" << rhs_parsed << ")";

    return klee::ExprVisitor::Action::skipChildren();
  }

  klee::ExprVisitor::Action visitURem(const klee::URemExpr &e) {
    assert(e.getNumKids() == 2);

    auto lhs = e.getKid(0);
    auto rhs = e.getKid(1);

    auto lhs_parsed = transpile(lhs, stack, is_signed);
    auto rhs_parsed = transpile(rhs, stack, is_signed);

    code << "(" << lhs_parsed << ")";
    code << " % ";
    code << "(" << rhs_parsed << ")";

    return klee::ExprVisitor::Action::skipChildren();
  }

  klee::ExprVisitor::Action visitSRem(const klee::SRemExpr &e) {
    assert(e.getNumKids() == 2);

    auto lhs = e.getKid(0);
    auto rhs = e.getKid(1);

    auto lhs_parsed = transpile(lhs, stack, true);
    auto rhs_parsed = transpile(rhs, stack, true);

    code << "(" << lhs_parsed << ")";
    code << " % ";
    code << "(" << rhs_parsed << ")";

    return klee::ExprVisitor::Action::skipChildren();
  }

  klee::ExprVisitor::Action visitNot(const klee::NotExpr &e) {
    assert(e.getNumKids() == 1);

    auto arg = e.getKid(0);
    auto arg_parsed = transpile(arg, stack, is_signed);
    code << "!" << arg_parsed;

    return klee::ExprVisitor::Action::skipChildren();
  }

  klee::ExprVisitor::Action visitAnd(const klee::AndExpr &e) {
    assert(e.getNumKids() == 2);

    auto lhs = e.getKid(0);
    auto rhs = e.getKid(1);

    auto lhs_parsed = transpile(lhs, stack, is_signed);
    auto rhs_parsed = transpile(rhs, stack, is_signed);

    code << "(" << lhs_parsed << ")";
    code << " & ";
    code << "(" << rhs_parsed << ")";

    return klee::ExprVisitor::Action::skipChildren();
  }

  klee::ExprVisitor::Action visitOr(const klee::OrExpr &e) {
    assert(e.getNumKids() == 2);

    auto lhs = e.getKid(0);
    auto rhs = e.getKid(1);

    auto lhs_parsed = transpile(lhs, stack, is_signed);
    auto rhs_parsed = transpile(rhs, stack, is_signed);

    code << "(" << lhs_parsed << ")";
    code << " | ";
    code << "(" << rhs_parsed << ")";

    return klee::ExprVisitor::Action::skipChildren();
  }

  klee::ExprVisitor::Action visitXor(const klee::XorExpr &e) {
    assert(e.getNumKids() == 2);

    auto lhs = e.getKid(0);
    auto rhs = e.getKid(1);

    auto lhs_parsed = transpile(lhs, stack, is_signed);
    auto rhs_parsed = transpile(rhs, stack, is_signed);

    code << "(" << lhs_parsed << ")";
    code << " ^ ";
    code << "(" << rhs_parsed << ")";

    return klee::ExprVisitor::Action::skipChildren();
  }

  klee::ExprVisitor::Action visitShl(const klee::ShlExpr &e) {
    assert(e.getNumKids() == 2);

    auto lhs = e.getKid(0);
    auto rhs = e.getKid(1);

    auto lhs_parsed = transpile(lhs, stack, is_signed);
    auto rhs_parsed = transpile(rhs, stack, is_signed);

    code << "(" << lhs_parsed << ")";
    code << " << ";
    code << "(" << rhs_parsed << ")";

    return klee::ExprVisitor::Action::skipChildren();
  }

  klee::ExprVisitor::Action visitLShr(const klee::LShrExpr &e) {
    assert(e.getNumKids() == 2);

    auto lhs = e.getKid(0);
    auto rhs = e.getKid(1);

    auto lhs_parsed = transpile(lhs, stack, is_signed);
    auto rhs_parsed = transpile(rhs, stack, is_signed);

    code << "(" << lhs_parsed << ")";
    code << " >> ";
    code << "(" << rhs_parsed << ")";

    return klee::ExprVisitor::Action::skipChildren();
  }

  klee::ExprVisitor::Action visitAShr(const klee::AShrExpr &e) {
    assert(e.getNumKids() == 2);

    auto lhs = e.getKid(0);
    auto rhs = e.getKid(1);

    auto sz = e.getWidth();
    assert(sz % 8 == 0);

    auto lhs_parsed = transpile(lhs, stack, is_signed);
    auto rhs_parsed = transpile(rhs, stack, is_signed);

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

  klee::ExprVisitor::Action visitEq(const klee::EqExpr &e) {
    assert(e.getNumKids() == 2);

    auto lhs = e.getKid(0);
    auto rhs = e.getKid(1);

    auto lhs_parsed = transpile(lhs, stack, is_signed);
    auto rhs_parsed = transpile(rhs, stack, is_signed);

    code << "(" << lhs_parsed << ")";
    code << " == ";
    code << "(" << rhs_parsed << ")";

    return klee::ExprVisitor::Action::skipChildren();
  }

  klee::ExprVisitor::Action visitNe(const klee::NeExpr &e) {
    assert(e.getNumKids() == 2);

    auto lhs = e.getKid(0);
    auto rhs = e.getKid(1);

    auto lhs_parsed = transpile(lhs, stack, is_signed);
    auto rhs_parsed = transpile(rhs, stack, is_signed);

    code << "(" << lhs_parsed << ")";
    code << " != ";
    code << "(" << rhs_parsed << ")";

    return klee::ExprVisitor::Action::skipChildren();
  }

  klee::ExprVisitor::Action visitUlt(const klee::UltExpr &e) {
    assert(e.getNumKids() == 2);

    auto lhs = e.getKid(0);
    auto rhs = e.getKid(1);

    auto lhs_parsed = transpile(lhs, stack, is_signed);
    auto rhs_parsed = transpile(rhs, stack, is_signed);

    code << "(" << lhs_parsed << ")";
    code << " < ";
    code << "(" << rhs_parsed << ")";

    return klee::ExprVisitor::Action::skipChildren();
  }

  klee::ExprVisitor::Action visitUle(const klee::UleExpr &e) {
    assert(e.getNumKids() == 2);

    auto lhs = e.getKid(0);
    auto rhs = e.getKid(1);

    auto lhs_parsed = transpile(lhs, stack, is_signed);
    auto rhs_parsed = transpile(rhs, stack, is_signed);

    code << "(" << lhs_parsed << ")";
    code << " <= ";
    code << "(" << rhs_parsed << ")";

    return klee::ExprVisitor::Action::skipChildren();
  }

  klee::ExprVisitor::Action visitUgt(const klee::UgtExpr &e) {
    assert(e.getNumKids() == 2);

    auto lhs = e.getKid(0);
    auto rhs = e.getKid(1);

    auto lhs_parsed = transpile(lhs, stack, is_signed);
    auto rhs_parsed = transpile(rhs, stack, is_signed);

    code << "(" << lhs_parsed << ")";
    code << " > ";
    code << "(" << rhs_parsed << ")";

    return klee::ExprVisitor::Action::skipChildren();
  }

  klee::ExprVisitor::Action visitUge(const klee::UgeExpr &e) {
    assert(e.getNumKids() == 2);

    auto lhs = e.getKid(0);
    auto rhs = e.getKid(1);

    auto lhs_parsed = transpile(lhs, stack, is_signed);
    auto rhs_parsed = transpile(rhs, stack, is_signed);

    code << "(" << lhs_parsed << ")";
    code << " >= ";
    code << "(" << rhs_parsed << ")";

    return klee::ExprVisitor::Action::skipChildren();
  }

  klee::ExprVisitor::Action visitSlt(const klee::SltExpr &e) {
    assert(e.getNumKids() == 2);

    auto lhs = e.getKid(0);
    auto rhs = e.getKid(1);

    auto lhs_parsed = transpile(lhs, stack, true);
    auto rhs_parsed = transpile(rhs, stack, true);

    code << "(" << lhs_parsed << ")";
    code << " < ";
    code << "(" << rhs_parsed << ")";

    return klee::ExprVisitor::Action::skipChildren();
  }

  klee::ExprVisitor::Action visitSle(const klee::SleExpr &e) {
    assert(e.getNumKids() == 2);

    auto lhs = e.getKid(0);
    auto rhs = e.getKid(1);

    auto lhs_parsed = transpile(lhs, stack, true);
    auto rhs_parsed = transpile(rhs, stack, true);

    code << "(" << lhs_parsed << ")";
    code << " <= ";
    code << "(" << rhs_parsed << ")";

    return klee::ExprVisitor::Action::skipChildren();
  }

  klee::ExprVisitor::Action visitSgt(const klee::SgtExpr &e) {
    assert(e.getNumKids() == 2);

    auto lhs = e.getKid(0);
    auto rhs = e.getKid(1);

    auto lhs_parsed = transpile(lhs, stack, true);
    auto rhs_parsed = transpile(rhs, stack, true);

    code << "(" << lhs_parsed << ")";
    code << " > ";
    code << "(" << rhs_parsed << ")";

    return klee::ExprVisitor::Action::skipChildren();
  }

  klee::ExprVisitor::Action visitSge(const klee::SgeExpr &e) {
    assert(e.getNumKids() == 2);

    auto lhs = e.getKid(0);
    auto rhs = e.getKid(1);

    auto lhs_parsed = transpile(lhs, stack, true);
    auto rhs_parsed = transpile(rhs, stack, true);

    code << "(" << lhs_parsed << ")";
    code << " >= ";
    code << "(" << rhs_parsed << ")";

    return klee::ExprVisitor::Action::skipChildren();
  }
};

void apply_changes(const klee::ref<klee::Expr> &before,
                   const klee::ref<klee::Expr> &after, stack_t &stack,
                   std::vector<std::string> &assignments) {
  assert(before->getWidth() == after->getWidth());

  std::stringstream assignment_stream;
  auto size = before->getWidth();

  for (unsigned int b = 0; b < size; b += 8) {
    auto before_byte =
        BDD::solver_toolbox.exprBuilder->Extract(before, b, klee::Expr::Int8);
    auto after_byte =
        BDD::solver_toolbox.exprBuilder->Extract(after, b, klee::Expr::Int8);

    if (BDD::solver_toolbox.are_exprs_always_equal(before_byte, after_byte)) {
      continue;
    }

    auto before_parsed = transpile(before_byte, stack);
    auto after_parsed = transpile(after_byte, stack);

    assignment_stream << before_parsed << " = " << after_parsed;
    assignments.push_back(assignment_stream.str());
    assignment_stream.str(std::string());
  }
}

std::string build(const klee::ref<klee::Expr> &e, stack_t &stack,
                  std::vector<std::string> &assignments) {
  std::stringstream assignment_stream;
  std::stringstream var_label;
  static int var_counter = 0;

  var_label << "var_" << var_counter;

  assert(!e.isNull());
  auto size = e->getWidth();
  assert(size % 8 == 0);

  assignment_stream << "uint8_t " << var_label.str() << "[" << size / 8 << "];";
  assignments.push_back(assignment_stream.str());
  assignment_stream.str(std::string());

  for (unsigned int b = 0; b < size; b += 8) {
    auto extract =
        BDD::solver_toolbox.exprBuilder->Extract(e, b, klee::Expr::Int8);
    assignment_stream << var_label.str() << "[" << b / 8 << "] = (uint8_t) ("
                      << transpile(extract, stack) << ");";
    assignments.push_back(assignment_stream.str());
    assignment_stream.str(std::string());
  }

  var_counter++;
  return var_label.str();
}

std::string transpile(const klee::ref<klee::Expr> &e, stack_t &stack,
                      bool is_signed) {
  if (e->getKind() == klee::Expr::Kind::Constant) {
    std::stringstream ss;
    auto constant = static_cast<klee::ConstantExpr *>(e.get());
    assert(constant->getWidth() <= 64);
    auto width = constant->getWidth();
    auto value = constant->getZExtValue();

    auto sign_bit = value >> (width - 1);

    if (is_signed && sign_bit) {
      uint64_t mask = 0;

      for (uint64_t i = 0u; i < width; i++) {
        mask <<= 1;
        mask |= 1;
      }

      ss << "-" << ((~value + 1) & mask);

      if (width > 32) {
        ss << "LL";
      }
    } else {
      ss << value;

      if (width > 32) {
        ss << "ULL";
      }
    }

    return ss.str();
  }

  auto stack_label = stack.get_by_value(e);
  if (stack_label.size()) {
    return stack_label;
  }

  KleeExprToC kleeExprToC(stack, is_signed);
  kleeExprToC.visit(e);

  auto code = kleeExprToC.get_code();

  if (!code.size()) {
    // error
    Log::err() << "Unable to transpile expression:\n";
    Log::err() << expr_to_string(e, true);
    exit(1);
  }

  return code;
}

std::string transpile(const klee::ref<klee::Expr> &e, stack_t &stack) {
  return transpile(e, stack, false);
}

int x86_Generator::close_if_clauses() {
  int closed = 0;

  assert(pending_ifs.size());

  while (pending_ifs.size()) {
    lvl--;
    pad(nf_process_stream);
    nf_process_stream << "}\n";

    closed++;

    auto if_clause = pending_ifs.top();
    pending_ifs.pop();

    if (if_clause) {
      pending_ifs.push(false);
      break;
    }
  }

  return closed;
}

void x86_Generator::allocate_map(call_t call, std::ostream &global_state,
                                 std::ostream &buffer) {
  assert(call.args["keq"].fn_ptr_name.first);
  assert(call.args["khash"].fn_ptr_name.first);
  assert(!call.args["capacity"].expr.isNull());
  assert(!call.args["map_out"].out.isNull());

  auto keq = call.args["keq"].fn_ptr_name.second;
  auto khash = call.args["khash"].fn_ptr_name.second;
  auto capacity = call.args["capacity"].expr;
  auto map_out = call.args["map_out"].out;

  static int map_counter = 0;

  std::stringstream map_stream;
  map_stream << "map_" << map_counter++;

  auto map_label = map_stream.str();
  klee::ref<klee::Expr> dummy;
  stack.add(map_label, dummy, map_out);

  global_state << "struct Map* " << map_label << ";\n";

  global_state << "bool " << keq << "(void* a, void* b);\n";
  global_state << "uint32_t " << khash << "(void* obj);\n\n";

  buffer << "map_allocate(";
  buffer << keq;
  buffer << ", " << khash;
  buffer << ", " << transpile(capacity, stack);
  buffer << ", &" << map_label;
  buffer << ")";
}

void x86_Generator::allocate_vector(call_t call, std::ostream &global_state,
                                    std::ostream &buffer) {
  assert(!call.args["elem_size"].expr.isNull());
  assert(!call.args["capacity"].expr.isNull());
  assert(call.args["init_elem"].fn_ptr_name.first);
  assert(!call.args["vector_out"].out.isNull());

  auto elem_size = call.args["elem_size"].expr;
  auto capacity = call.args["capacity"].expr;
  auto init_elem = call.args["init_elem"].fn_ptr_name.second;
  auto vector_out = call.args["vector_out"].out;

  static int vector_counter = 0;

  std::stringstream vector_stream;
  vector_stream << "vector_" << vector_counter++;

  auto vector_label = vector_stream.str();
  klee::ref<klee::Expr> dummy;
  stack.add(vector_label, dummy, vector_out);

  global_state << "struct Vector* " << vector_label << ";\n";
  global_state << "void " << init_elem << "(void* obj);\n\n";

  buffer << "vector_allocate(";
  buffer << transpile(elem_size, stack);
  buffer << ", " << transpile(capacity, stack);
  buffer << ", " << init_elem;
  buffer << ", &" << vector_label;
  buffer << ")";
}

void x86_Generator::allocate_dchain(call_t call, std::ostream &global_state,
                                    std::ostream &buffer) {
  assert(!call.args["index_range"].expr.isNull());
  assert(!call.args["chain_out"].out.isNull());

  auto index_range = call.args["index_range"].expr;
  auto chain_out = call.args["chain_out"].out;

  static int dchain_counter = 0;

  std::stringstream dchain_stream;
  dchain_stream << "dchain_" << dchain_counter++;

  auto dchain_label = dchain_stream.str();
  klee::ref<klee::Expr> dummy;
  stack.add(dchain_label, dummy, chain_out);

  global_state << "struct DoubleChain* " << dchain_label << ";\n";

  buffer << "dchain_allocate(";
  buffer << transpile(index_range, stack);
  buffer << ", &" << dchain_label;
  buffer << ")";
}

void x86_Generator::allocate_cht(call_t call, std::ostream &global_state,
                                 std::ostream &buffer) {
  assert(!call.args["cht"].expr.isNull());
  assert(!call.args["cht_height"].expr.isNull());
  assert(!call.args["backend_capacity"].expr.isNull());

  auto vector_addr = call.args["cht"].expr;
  auto chr_height = call.args["cht_height"].expr;
  auto backend_capacity = call.args["backend_capacity"].expr;

  buffer << "cht_fill_cht(";
  buffer << ", " << transpile(vector_addr, stack);
  buffer << ", " << transpile(chr_height, stack);
  buffer << ", " << transpile(backend_capacity, stack);
  buffer << ")";
}

void x86_Generator::allocate(const ExecutionPlan &ep) {
  auto node = ep.get_bdd().get_init();

  while (node) {
    switch (node->get_type()) {
    case BDD::Node::NodeType::CALL: {
      auto call_node = static_cast<const BDD::Call *>(node.get());
      auto call = call_node->get_call();

      pad(nf_init_stream);
      nf_init_stream << "if (";
      if (call.function_name == "map_allocate") {
        allocate_map(call, global_state_stream, nf_init_stream);
      }

      else if (call.function_name == "vector_allocate") {
        allocate_vector(call, global_state_stream, nf_init_stream);
      }

      else if (call.function_name == "dchain_allocate") {
        allocate_dchain(call, global_state_stream, nf_init_stream);
      }

      else if (call.function_name == "cht_fill_cht") {
        allocate_cht(call, global_state_stream, nf_init_stream);
      }

      else {
        assert(false);
      }

      nf_init_stream << ") {\n";
      lvl++;

      break;
    }

    case BDD::Node::NodeType::BRANCH: {

      break;
    }

    case BDD::Node::NodeType::RETURN_INIT: {
      pad(nf_init_stream);
      nf_init_stream << "return true;\n";
      while (lvl > 1) {
        lvl--;
        pad(nf_init_stream);
        nf_init_stream << "}\n";
      }
      pad(nf_init_stream);
      nf_init_stream << "return false;\n";
      break;
    }

    default:
      assert(false);
    }

    node = node->get_next();
  }
}

std::pair<bool, uint64_t>
x86_Generator::get_expiration_time(klee::ref<klee::Expr> libvig_obj) const {
  for (auto et : expiration_times) {
    auto obj = et.first;
    auto time = et.second;

    auto obj_extracted = BDD::solver_toolbox.exprBuilder->Extract(
        obj, 0, libvig_obj->getWidth());

    if (BDD::solver_toolbox.are_exprs_always_equal(obj_extracted, libvig_obj)) {
      return std::make_pair(true, time);
    }
  }

  return std::make_pair(false, 0);
}

std::vector<x86_Generator::p4_table> x86_Generator::get_associated_p4_tables(
    klee::ref<klee::Expr> libvig_obj) const {
  std::vector<x86_Generator::p4_table> tables;

  assert(original_ep);
  assert(original_ep->get_root());

  std::vector<ExecutionPlanNode_ptr> nodes = {original_ep->get_root()};

  while (nodes.size()) {
    auto node = nodes[0];
    nodes.erase(nodes.begin());

    auto module = node->get_module();

    if (module->get_type() ==
        Module::ModuleType::BMv2SimpleSwitchgRPC_TableLookup) {
      auto table_lookup =
          static_cast<targets::BMv2SimpleSwitchgRPC::TableLookup *>(
              module.get());

      auto bdd_node = module->get_node();

      assert(bdd_node);
      assert(bdd_node->get_type() == BDD::Node::NodeType::CALL);

      auto bdd_call = static_cast<const BDD::Call *>(bdd_node.get());
      auto call = bdd_call->get_call();

      auto obj = table_lookup->get_obj();
      assert(!obj.isNull());

      if (!BDD::solver_toolbox.are_exprs_always_equal(libvig_obj, obj)) {
        goto skip;
      }

      auto bdd_function = table_lookup->get_bdd_function();
      auto table_id = table_lookup->get_table_id();
      auto keys = table_lookup->get_keys();
      auto params = table_lookup->get_params();

      auto table_name = build_table_name(bdd_function, table_id);
      auto table_label = build_table_label(bdd_function, table_id);

      std::stringstream tag_name;
      tag_name << table_label;
      tag_name << "_tag";

      auto found_it = std::find_if(tables.begin(), tables.end(),
                                   [&](x86_Generator::p4_table table) {
                                     return table.name == table_label;
                                   });

      if (found_it != tables.end()) {
        goto skip;
      }

      x86_Generator::p4_table table;
      table.name = table_name;
      table.label = table_label;
      table.tag = tag_name.str();
      table.n_keys = keys.size();
      table.n_params = params.size();

      tables.push_back(table);
    }

  skip:
    auto next = node->get_next();
    nodes.insert(nodes.end(), next.begin(), next.end());
  }

  return tables;
}

klee::ref<klee::Expr>
get_readLSB_vigor_device(std::vector<klee::ConstraintManager> managers) {
  for (auto manager : managers) {
    for (auto constraint : manager) {
      RetrieveSymbols retriever(true);
      retriever.visit(constraint);

      auto symbols = retriever.get_retrieved_strings();

      if (symbols.size() != 1 || *symbols.begin() != "VIGOR_DEVICE") {
        continue;
      }

      if (retriever.get_retrieved_readLSB().size() == symbols.size()) {
        return retriever.get_retrieved_readLSB()[0];
      }
    }
  }

  assert(false && "Could not find VIGOR_DEVICE in constraints");
}

void x86_Generator::fill_is_controller() {
  assert(original_ep);
  assert(original_ep->get_root());

  std::vector<ExecutionPlanNode_ptr> nodes = {original_ep->get_root()};

  while (nodes.size()) {
    auto node = nodes[0];
    nodes.erase(nodes.begin());

    auto module = node->get_module();

    if (module->get_target() != Target::x86) {
      is_controller = std::make_pair(true, module->get_target());
      return;
    }

    auto next = node->get_next();
    nodes.insert(nodes.end(), next.begin(), next.end());
  }
}

void x86_Generator::build_runtime_configure() {
  assert(original_ep);
  assert(original_ep->get_root());

  std::unordered_set<uint64_t> devices;

  struct table_t {
    struct libvig_obj_t {
      enum obj_type_t { MAP, VECTOR, DCHAIN };

      std::string obj_label;
      obj_type_t obj_type;
    };

    std::string name;
    std::vector<libvig_obj_t> objs;
  };

  std::vector<table_t> tables;

  std::vector<ExecutionPlanNode_ptr> nodes = {original_ep->get_root()};

  while (nodes.size()) {
    auto node = nodes[0];
    nodes.erase(nodes.begin());

    auto module = node->get_module();

    if (module->get_type() == Module::ModuleType::x86_CurrentTime) {
      auto bdd_node = module->get_node();
      auto constraint_managers = bdd_node->get_constraints();

      auto vigor_device = get_readLSB_vigor_device(constraint_managers);

      for (auto manager : constraint_managers) {
        auto value = BDD::solver_toolbox.value_from_expr(vigor_device, manager);
        auto eq = BDD::solver_toolbox.exprBuilder->Eq(
            vigor_device, BDD::solver_toolbox.exprBuilder->Constant(
                              value, vigor_device->getWidth()));

        auto always_eq = BDD::solver_toolbox.is_expr_always_true(manager, eq);
        assert(always_eq);

        devices.insert(value);
      }
    }

    if (module->get_type() ==
        Module::ModuleType::BMv2SimpleSwitchgRPC_TableLookup) {
      auto table_lookup =
          static_cast<targets::BMv2SimpleSwitchgRPC::TableLookup *>(
              module.get());

      auto bdd_function = table_lookup->get_bdd_function();
      auto table_id = table_lookup->get_table_id();
      auto obj = table_lookup->get_obj();

      auto obj_label = stack.get_label(obj);

      auto table_name = build_table_name(bdd_function, table_id);

      table_t::libvig_obj_t::obj_type_t type;

      if (bdd_function == "map_get") {
        type = table_t::libvig_obj_t::MAP;
      } else if (bdd_function == "vector_borrow" ||
                 bdd_function == "vector_return") {
        type = table_t::libvig_obj_t::VECTOR;
      } else if (bdd_function == "dchain_is_index_allocated") {
        type = table_t::libvig_obj_t::DCHAIN;
      } else {
        assert(false && "TODO");
      }

      table_t table{table_name, std::vector<table_t::libvig_obj_t>{
                                    table_t::libvig_obj_t{obj_label, type}}};

      tables.push_back(table);
    }

    auto next = node->get_next();
    nodes.insert(nodes.end(), next.begin(), next.end());
  }

  pad(runtime_configure_stream);
  runtime_configure_stream
      << "bool synapse_runtime_config(synapse_config_t *config) {\n";
  lvl++;

  pad(runtime_configure_stream);
  runtime_configure_stream << "config->devices_sz";
  runtime_configure_stream << " = ";
  runtime_configure_stream << devices.size();
  runtime_configure_stream << ";\n";

  pad(runtime_configure_stream);
  runtime_configure_stream << "config->devices";
  runtime_configure_stream << " = ";
  runtime_configure_stream << "(uint32_t*) malloc(";
  runtime_configure_stream << "sizeof(uint32_t) * config->devices_sz";
  runtime_configure_stream << ")";
  runtime_configure_stream << ";\n";

  auto i = 0u;
  for (auto device : devices) {
    pad(runtime_configure_stream);
    runtime_configure_stream << "config->devices[" << i << "]";
    runtime_configure_stream << " = ";
    runtime_configure_stream << device;
    runtime_configure_stream << ";\n";

    i++;
  }

  pad(runtime_configure_stream);
  runtime_configure_stream << "config->bmv2_tables_sz";
  runtime_configure_stream << " = ";
  runtime_configure_stream << tables.size();
  runtime_configure_stream << ";\n";

  pad(runtime_configure_stream);
  runtime_configure_stream << "config->bmv2_tables";
  runtime_configure_stream << " = ";
  runtime_configure_stream << "(synapse_bmv2_table_t*) malloc(";
  runtime_configure_stream << "sizeof(synapse_bmv2_table_t) * ";
  runtime_configure_stream << "config->bmv2_tables_sz";
  runtime_configure_stream << ")";
  runtime_configure_stream << ";\n";

  i = 0;
  for (auto table : tables) {
    pad(runtime_configure_stream);
    runtime_configure_stream << "config->bmv2_tables[" << i << "]";
    runtime_configure_stream << ".name";
    runtime_configure_stream << ".str = \"" << table.name << "\";\n";

    pad(runtime_configure_stream);
    runtime_configure_stream << "config->bmv2_tables[" << i << "]";
    runtime_configure_stream << ".name";
    runtime_configure_stream << ".sz = " << table.name.size() << ";\n";

    pad(runtime_configure_stream);
    runtime_configure_stream << "config->bmv2_tables[" << i << "]";
    runtime_configure_stream << ".tag = 0;\n";

    pad(runtime_configure_stream);
    runtime_configure_stream << "config->bmv2_tables[" << i << "]";
    runtime_configure_stream << ".libvig_objs = ";
    runtime_configure_stream << "(libvig_obj_t*) malloc(";
    runtime_configure_stream << "sizeof(libvig_obj_t) * " << table.objs.size();
    runtime_configure_stream << ");\n";

    pad(runtime_configure_stream);
    runtime_configure_stream << "config->bmv2_tables[" << i << "]";
    runtime_configure_stream << ".libvig_objs_sz = " << table.objs.size()
                             << ";\n";

    auto j = 0u;
    for (auto obj : table.objs) {
      pad(runtime_configure_stream);
      runtime_configure_stream << "config->bmv2_tables[" << i << "]";
      runtime_configure_stream << ".libvig_objs[" << j << "]";
      runtime_configure_stream << ".ptr = (void*) " << obj.obj_label << ";\n";

      pad(runtime_configure_stream);
      runtime_configure_stream << "config->bmv2_tables[" << i << "]";
      runtime_configure_stream << ".libvig_objs[" << j << "]";
      runtime_configure_stream << ".type = ";
      runtime_configure_stream << "LIBVIG_";
      switch (obj.obj_type) {
      case table_t::libvig_obj_t::MAP:
        runtime_configure_stream << "MAP";
        break;
      case table_t::libvig_obj_t::VECTOR:
        runtime_configure_stream << "VECTOR";
        break;
      case table_t::libvig_obj_t::DCHAIN:
        runtime_configure_stream << "DCHAIN";
        break;
      }
      runtime_configure_stream << ";\n";

      j++;
    }

    i++;
  }

  pad(runtime_configure_stream);
  runtime_configure_stream << "return true;\n";

  lvl--;
  pad(runtime_configure_stream);
  runtime_configure_stream << "}";
}

void x86_Generator::visit(ExecutionPlan ep) {
  fill_is_controller();

  lvl = code_builder.get_indentation_level(MARKER_NF_INIT);

  allocate(ep);

  if (is_controller.first &&
      is_controller.second == Target::BMv2SimpleSwitchgRPC) {
    lvl = code_builder.get_indentation_level(MARKER_RUNTIME_CONFIGURE);
    build_runtime_configure();
  }

  std::string vigor_device_label = "VIGOR_DEVICE";
  std::string packet_label = "p";
  std::string pkt_len_label = "pkt_len";
  std::string now_label = "now";

  stack.push();

  stack.add(vigor_device_label);
  stack.add(packet_label);
  stack.add(pkt_len_label);
  stack.add(now_label);

  lvl = code_builder.get_indentation_level(MARKER_NF_PROCESS);

  stack.push();
  ExecutionPlanVisitor::visit(ep);
  stack.pop();

  code_builder.fill_mark(MARKER_GLOBAL_STATE, global_state_stream.str());

  code_builder.fill_mark(MARKER_RUNTIME_CONFIGURE,
                         runtime_configure_stream.str());

  code_builder.fill_mark(MARKER_NF_INIT, nf_init_stream.str());
  code_builder.fill_mark(MARKER_NF_PROCESS, nf_process_stream.str());
}

void x86_Generator::visit(const ExecutionPlanNode *ep_node) {
  auto mod = ep_node->get_module();
  auto next = ep_node->get_next();

  mod->visit(*this);

  assert(next.size() <= 1 ||
         next[1]->get_module()->get_type() == Module::ModuleType::x86_Else);

  for (auto branch : next) {
    branch->visit(*this);
  }
}

void x86_Generator::visit(const targets::x86::MapGet *node) {
  auto map_addr = node->get_map_addr();
  auto key = node->get_key();
  auto map_has_this_key = node->get_map_has_this_key();
  auto value_out = node->get_value_out();

  auto generated_symbols = node->get_generated_symbols();

  assert(!map_addr.isNull());
  assert(!key.isNull());
  assert(!map_has_this_key.isNull());
  assert(!value_out.isNull());

  auto map = stack.get_label(map_addr);
  if (!map.size()) {
    stack.err_dump();
    assert(false && "Not found in stack");
  }

  assert(generated_symbols.size() == 2);

  auto map_has_this_key_label =
      get_label(generated_symbols, "map_has_this_key");

  auto allocated_index_label = get_label(generated_symbols, "allocated_index");

  stack.add(map_has_this_key_label, map_has_this_key);
  stack.add(allocated_index_label, value_out);

  std::vector<std::string> key_assignments;
  auto key_label = build(key, stack, key_assignments);

  for (auto key_assignment : key_assignments) {
    pad(nf_process_stream);
    nf_process_stream << key_assignment << "\n";
  }

  pad(nf_process_stream);
  nf_process_stream << "int " << allocated_index_label << ";\n";

  pad(nf_process_stream);
  nf_process_stream << "int " << map_has_this_key_label;
  nf_process_stream << " = ";
  nf_process_stream << "map_get(";
  nf_process_stream << map;
  nf_process_stream << ", (void*)" << key_label;
  nf_process_stream << ", &" << allocated_index_label;
  nf_process_stream << ");\n";
}

void x86_Generator::visit(const targets::x86::CurrentTime *node) {
  auto generated_symbols = node->get_generated_symbols();
  assert(generated_symbols.size() == 1);

  auto next_time_label = get_label(generated_symbols, "next_time");

  stack.cp_var_to_code_translation.insert({next_time_label, "now"});
  stack.set_value(next_time_label, node->get_time());
}

void x86_Generator::visit(const targets::x86::PacketBorrowNextChunk *node) {
  auto p_addr = node->get_p_addr();
  auto chunk = node->get_chunk();
  auto chunk_addr = node->get_chunk_addr();
  auto length = node->get_length();

  assert(!p_addr.isNull());
  assert(!chunk.isNull());
  assert(!chunk_addr.isNull());
  assert(!length.isNull());

  auto p_label = "p";
  stack.set_addr(p_label, p_addr);

  static int chunk_counter = 0;

  std::stringstream chunk_stream;
  chunk_stream << "chunk_" << chunk_counter;

  auto chunk_label = chunk_stream.str();
  stack.add(chunk_label, chunk, chunk_addr);

  pad(nf_process_stream);

  nf_process_stream << "uint8_t* " << chunk_label << " = (uint8_t*)";
  nf_process_stream << "nf_borrow_next_chunk(";
  nf_process_stream << p_label;
  nf_process_stream << ", " << transpile(length, stack);
  nf_process_stream << ");\n";

  chunk_counter++;
}

void x86_Generator::visit(const targets::x86::PacketGetMetadata *node) {
  auto metadata = node->get_metadata();

  assert(!metadata.isNull());

  auto code_id_metadata_label = std::string("code_id_meta");
  auto metadata_key_label = std::string("metadata_key");
  auto metadata_key = std::string("code_id");

  stack.add(code_id_metadata_label, metadata);

  pad(nf_process_stream);
  nf_process_stream << "string_t " << metadata_key_label;
  nf_process_stream << " = {";
  nf_process_stream << " .str = ";
  nf_process_stream << "\"" << metadata_key << "\"";
  nf_process_stream << ", .sz = " << metadata_key.size();
  nf_process_stream << " };\n";

  pad(nf_process_stream);
  nf_process_stream << "string_ptr_t " << code_id_metadata_label << "_str;\n";

  pad(nf_process_stream);
  nf_process_stream << "synapse_runtime_pkt_in_get_meta_by_name(";
  nf_process_stream << metadata_key_label;
  nf_process_stream << ", &" << code_id_metadata_label << "_str";
  nf_process_stream << ");\n";

  pad(nf_process_stream);
  nf_process_stream << "int " << code_id_metadata_label;
  nf_process_stream << " = ";
  nf_process_stream << "synapse_decode_p4_uint32(";
  nf_process_stream << code_id_metadata_label << "_str";
  nf_process_stream << ");\n";
}

void x86_Generator::visit(const targets::x86::PacketReturnChunk *node) {
  auto chunk_addr = node->get_chunk_addr();
  assert(!chunk_addr.isNull());

  auto chunk = node->get_chunk();
  assert(!chunk.isNull());

  auto before_chunk = stack.get_value(chunk_addr);
  assert(!before_chunk.isNull());

  auto label = stack.get_label(chunk_addr);
  assert(label.size());

  auto size = chunk->getWidth();
  for (unsigned b = 0; b < size; b += 8) {
    auto chunk_byte =
        BDD::solver_toolbox.exprBuilder->Extract(chunk, b, klee::Expr::Int8);
    auto before_chunk_byte = BDD::solver_toolbox.exprBuilder->Extract(
        before_chunk, b, klee::Expr::Int8);

    if (!BDD::solver_toolbox.are_exprs_always_equal(chunk_byte,
                                                    before_chunk_byte)) {
      pad(nf_process_stream);
      nf_process_stream << label << "[" << b / 8 << "]";
      nf_process_stream << " = ";
      nf_process_stream << transpile(chunk_byte, stack);
      nf_process_stream << ";\n";
    }
  }

  auto chunk_label = stack.get_label(chunk_addr);

  pad(nf_process_stream);
  nf_process_stream << "packet_return_chunk(";
  nf_process_stream << "*p";
  nf_process_stream << ", (void*) " << chunk_label;
  nf_process_stream << ");\n";
}

void x86_Generator::visit(const targets::x86::If *node) {
  auto condition = node->get_condition();

  pad(nf_process_stream);
  nf_process_stream << "if (";
  nf_process_stream << transpile(condition, stack);
  nf_process_stream << ") {\n";
  lvl++;

  stack.push();
  pending_ifs.push(true);
}

void x86_Generator::visit(const targets::x86::Then *node) {}

void x86_Generator::visit(const targets::x86::Else *node) {
  pad(nf_process_stream);
  nf_process_stream << "else {\n";
  lvl++;

  stack.push();
}

void x86_Generator::visit(const targets::x86::Forward *node) {
  pad(nf_process_stream);
  nf_process_stream << "return " << node->get_port() << ";\n";

  auto closed = close_if_clauses();
  for (int i = 0; i < closed; i++) {
    stack.pop();
  }
}

void x86_Generator::visit(const targets::x86::Broadcast *node) {
  pad(nf_process_stream);
  nf_process_stream << "return 65535;\n";

  auto closed = close_if_clauses();
  for (int i = 0; i < closed; i++) {
    stack.pop();
  }
}

void x86_Generator::visit(const targets::x86::Drop *node) {
  pad(nf_process_stream);
  nf_process_stream << "return device;\n";

  auto closed = close_if_clauses();
  for (int i = 0; i < closed; i++) {
    stack.pop();
  }
}

uint64_t get_expiration_time_value(klee::ref<klee::Expr> time) {
  RetrieveSymbols retriever(true);
  retriever.visit(time);

  auto readLSBs = retriever.get_retrieved_readLSB();
  assert(readLSBs.size() == 1);

  auto next_time = readLSBs[0];
  auto expiration_time_expr =
      BDD::solver_toolbox.exprBuilder->Sub(time, next_time);
  auto expiration_time =
      BDD::solver_toolbox.value_from_expr(expiration_time_expr);

  auto always_eq = BDD::solver_toolbox.are_exprs_always_equal(
      BDD::solver_toolbox.exprBuilder->Constant(
          expiration_time, expiration_time_expr->getWidth()),
      expiration_time_expr);
  assert(always_eq);

  // 2's complement
  expiration_time = ~expiration_time + 1;

  return expiration_time;
}

void x86_Generator::visit(const targets::x86::ExpireItemsSingleMap *node) {
  auto dchain_addr = node->get_dchain_addr();
  auto vector_addr = node->get_vector_addr();
  auto map_addr = node->get_map_addr();
  auto time = node->get_time();
  auto number_of_freed_flows = node->get_number_of_freed_flows();
  auto generated_symbols = node->get_generated_symbols();

  assert(!dchain_addr.isNull());
  assert(!vector_addr.isNull());
  assert(!map_addr.isNull());
  assert(!time.isNull());
  assert(!number_of_freed_flows.isNull());

  // get expiration time
  auto expiration_time = get_expiration_time_value(time);

  expiration_times.emplace_back(dchain_addr, expiration_time);
  expiration_times.emplace_back(vector_addr, expiration_time);
  expiration_times.emplace_back(map_addr, expiration_time);

  assert(generated_symbols.size() == 1);

  auto number_of_freed_flows_label =
      get_label(generated_symbols, "number_of_freed_flows");

  stack.add(number_of_freed_flows_label, number_of_freed_flows);

  // Kind of a hack: symbolic execution tells us that
  // the success of a dchain_allocate_new_index is
  // related to both out_of_space symbol and
  // number_of_freed_flows symbol.
  // However, the dchain_allocate_new_index returns
  // a bool that tells if the function was able to allocate.
  // So we just ignore this symbol.
  pad(nf_process_stream);
  nf_process_stream << "// hack\n";
  pad(nf_process_stream);
  nf_process_stream << "int " << number_of_freed_flows_label;
  nf_process_stream << " = 0;\n";

  // expiration is controlled by the switch
  // TODO: if a specific object is not on the switch, its expiration time
  // must be controlled by the controller
  if (is_controller.first &&
      is_controller.second == Target::BMv2SimpleSwitchgRPC) {
    return;
  }

  auto dchain = stack.get_label(dchain_addr);
  if (!dchain.size()) {
    stack.err_dump();
    assert(false && "Not found in stack");
  }

  auto vector = stack.get_label(vector_addr);
  if (!vector.size()) {
    stack.err_dump();
    assert(false && "Not found in stack");
  }

  auto map = stack.get_label(map_addr);
  if (!map.size()) {
    stack.err_dump();
    assert(false && "Not found in stack");
  }

  pad(nf_process_stream);
  nf_process_stream << "expire_items_single_map(";
  nf_process_stream << dchain;
  nf_process_stream << ", " << vector;
  nf_process_stream << ", " << map;
  nf_process_stream << ", " << transpile(time, stack, true);
  nf_process_stream << ");\n";
}

void x86_Generator::visit(const targets::x86::RteEtherAddrHash *node) {
  auto obj = node->get_obj();
  auto hash = node->get_hash();
  auto generated_symbols = node->get_generated_symbols();

  assert(!obj.isNull());
  assert(!hash.isNull());

  assert(generated_symbols.size() == 1);

  auto hash_label = get_label(generated_symbols, "rte_ether_addr_hash");
  stack.add(hash_label, hash);

  std::vector<std::string> obj_assignments;
  auto obj_label = build(obj, stack, obj_assignments);

  for (auto obj_assignment : obj_assignments) {
    pad(nf_process_stream);
    nf_process_stream << obj_assignment << "\n";
  }

  pad(nf_process_stream);
  nf_process_stream << "uint32_t " << hash_label;
  nf_process_stream << " = ";
  nf_process_stream << "rte_ether_addr_hash(";
  nf_process_stream << "(void*) &" << obj_label;
  nf_process_stream << ");\n";
}

void x86_Generator::visit(const targets::x86::DchainRejuvenateIndex *node) {
  auto dchain_addr = node->get_dchain_addr();
  auto time = node->get_time();
  auto index = node->get_index();

  assert(!dchain_addr.isNull());
  assert(!time.isNull());
  assert(!index.isNull());

  auto dchain = stack.get_label(dchain_addr);
  if (!dchain.size()) {
    stack.err_dump();
    assert(false && "Not found in stack");
  }

  pad(nf_process_stream);
  nf_process_stream << "dchain_rejuvenate_index(";
  nf_process_stream << dchain;
  nf_process_stream << ", " << transpile(index, stack);
  nf_process_stream << ", " << transpile(time, stack);
  nf_process_stream << ");\n";
}

klee::ref<klee::Expr> get_future_vector_value(BDD::BDDNode_ptr root,
                                              klee::ref<klee::Expr> vector) {
  std::vector<BDD::BDDNode_ptr> nodes{root};

  while (nodes.size()) {
    auto node = nodes[0];
    nodes.erase(nodes.begin());

    if (node->get_type() == BDD::Node::NodeType::BRANCH) {
      auto branch_node = static_cast<BDD::Branch *>(node.get());

      nodes.push_back(branch_node->get_on_true());
      nodes.push_back(branch_node->get_on_false());

      continue;
    }

    else if (node->get_type() == BDD::Node::NodeType::CALL) {
      auto call_node = static_cast<BDD::Call *>(node.get());
      auto call = call_node->get_call();

      if (call.function_name != "vector_return") {
        nodes.push_back(call_node->get_next());
        continue;
      }

      auto eq = BDD::solver_toolbox.are_exprs_always_equal(
          vector, call.args["vector"].expr);

      if (!eq) {
        nodes.push_back(call_node->get_next());
        continue;
      }

      return call.args["value"].in;
    }
  }

  assert(false && "vector_return not found");
}

void x86_Generator::visit(const targets::x86::VectorBorrow *node) {
  auto vector_addr = node->get_vector_addr();
  auto index = node->get_index();
  auto value_out = node->get_value_out();
  auto borrowed_cell = node->get_borrowed_cell();
  auto generated_symbols = node->get_generated_symbols();

  assert(!vector_addr.isNull());
  assert(!index.isNull());
  assert(!value_out.isNull());
  assert(!borrowed_cell.isNull());

  auto vector = stack.get_label(vector_addr);
  if (!vector.size()) {
    stack.err_dump();
    assert(false && "Not found in stack");
  }

  auto borrowed_cell_sz = borrowed_cell->getWidth();
  assert(borrowed_cell_sz % 8 == 0);

  assert(generated_symbols.size() == 1);

  auto value_out_label = get_label(generated_symbols, "vector_data_reset");
  stack.add(value_out_label, borrowed_cell, value_out);

  auto new_value = get_future_vector_value(node->get_node(), vector_addr);

  std::vector<std::string> assignments;
  apply_changes(borrowed_cell, new_value, stack, assignments);
  stack.add(value_out_label, new_value, value_out);

  pad(nf_process_stream);
  nf_process_stream << "uint8_t " << value_out_label << "["
                    << borrowed_cell_sz / 8 << "];\n";

  pad(nf_process_stream);
  nf_process_stream << "vector_borrow(";
  nf_process_stream << vector;
  nf_process_stream << ", " << transpile(index, stack);
  nf_process_stream << ", (void **)&" << value_out_label;
  nf_process_stream << ");\n";

  for (auto assignment : assignments) {
    pad(nf_process_stream);
    nf_process_stream << assignment << ";\n";
  }
}

void x86_Generator::visit(const targets::x86::VectorReturn *node) {
  auto vector_addr = node->get_vector_addr();
  auto index = node->get_index();
  auto value_addr = node->get_value_addr();
  auto value = node->get_value();

  assert(!vector_addr.isNull());
  assert(!index.isNull());
  assert(!value_addr.isNull());

  auto vector = stack.get_label(vector_addr);
  if (!vector.size()) {
    stack.err_dump();
    assert(false && "Not found in stack");
  }

  auto value_label = stack.get_label(value_addr);
  if (!value_label.size()) {
    stack.err_dump();
    assert(node->get_node());

    Log::err() << "Node:  " << node->get_node()->dump(true) << "\n";
    Log::err() << "Expr: " << expr_to_string(value_addr, true) << "\n";
    Log::err() << "Label:  " << value_label << "\n";
    assert(false && "Not found in stack");
  }

  pad(nf_process_stream);
  nf_process_stream << "vector_return(";
  nf_process_stream << vector;
  nf_process_stream << ", " << transpile(index, stack);
  nf_process_stream << ", (void *)" << value_label;
  nf_process_stream << ");\n";

  if (is_controller.first &&
      is_controller.second == Target::BMv2SimpleSwitchgRPC) {
    issue_write_to_switch(vector_addr, index, value);
  }
}

void x86_Generator::visit(const targets::x86::DchainAllocateNewIndex *node) {
  auto dchain_addr = node->get_dchain_addr();
  auto time = node->get_time();
  auto index_out = node->get_index_out();
  auto success = node->get_success();
  auto generated_symbols = node->get_generated_symbols();

  static int success_counter = 0;
  std::stringstream success_label;

  assert(!dchain_addr.isNull());
  assert(!time.isNull());
  assert(!index_out.isNull());
  assert(!success.isNull());

  success_label << "success_" << success_counter;
  success_counter++;

  auto dchain = stack.get_label(dchain_addr);
  if (!dchain.size()) {
    stack.err_dump();
    assert(false && "Not found in stack");
  }

  assert(generated_symbols.size() == 2);

  auto out_of_space_label = get_label(generated_symbols, "out_of_space");
  auto new_index_label = get_label(generated_symbols, "new_index");

  stack.add(out_of_space_label, success);
  stack.add(new_index_label, index_out);

  pad(nf_process_stream);
  nf_process_stream << "int " << new_index_label << ";\n";

  pad(nf_process_stream);
  nf_process_stream << "int " << success_label.str();
  nf_process_stream << " = ";
  nf_process_stream << "dchain_allocate_new_index(";
  nf_process_stream << dchain;
  nf_process_stream << ", &" << new_index_label;
  nf_process_stream << ", " << transpile(time, stack);
  nf_process_stream << ");\n";

  pad(nf_process_stream);
  nf_process_stream << "int " << out_of_space_label;
  nf_process_stream << " = !" << success_label.str();
  nf_process_stream << ";\n";
}

void x86_Generator::issue_write_to_switch(klee::ref<klee::Expr> libvig_obj,
                                          klee::ref<klee::Expr> key,
                                          klee::ref<klee::Expr> value) {
  auto tables = get_associated_p4_tables(libvig_obj);

  for (auto table : tables) {
    pad(nf_process_stream);
    nf_process_stream << "{\n";
    lvl++;

    assert(table.n_params == 1 && "TODO table merging");

    pad(nf_process_stream);
    nf_process_stream << "string_t table_name";
    nf_process_stream << " = { ";
    nf_process_stream << ".str = \"" << table.name << "\"";
    nf_process_stream << ", .sz = " << table.name.size();
    nf_process_stream << " };\n";

    for (auto byte = 0u; byte < key->getWidth() / 8; byte++) {
      std::stringstream key_byte_name;
      key_byte_name << "key_byte_" << byte;

      auto key_byte_expr = BDD::solver_toolbox.exprBuilder->Extract(
          key, byte * 8, klee::Expr::Int8);
      auto key_byte_expr_transpiled = transpile(key_byte_expr, stack);

      pad(nf_process_stream);
      nf_process_stream << "string_t " << key_byte_name.str() << "_name";
      nf_process_stream << " = { ";
      nf_process_stream << ".str = \"" << key_byte_name.str() << "\"";
      nf_process_stream << ", .sz = " << key_byte_name.str().size();
      nf_process_stream << " };\n";

      pad(nf_process_stream);
      nf_process_stream << "string_ptr_t " << key_byte_name.str() << "_value";
      nf_process_stream << " = ";
      nf_process_stream << "synapse_runtime_wrappers_p4_uint32_new(";
      nf_process_stream << "(uint32_t) (";
      nf_process_stream << key_byte_expr_transpiled;
      nf_process_stream << "))->bytes";
      nf_process_stream << ";\n";
    }

    nf_process_stream << "\n";

    pad(nf_process_stream);
    nf_process_stream << "pair_t keys[" << key->getWidth() / 8 << "] = {\n";
    lvl++;

    for (auto byte = 0u; byte < key->getWidth() / 8; byte++) {
      std::stringstream key_byte_name;
      key_byte_name << "key_byte_" << byte;

      pad(nf_process_stream);

      nf_process_stream << "{";
      nf_process_stream << " .left = (void*) &" << key_byte_name.str()
                        << "_name";
      nf_process_stream << ", .right = (void*) " << key_byte_name.str()
                        << "_value";
      nf_process_stream << " }";

      if (byte != (key->getWidth() / 8) - 1) {
        nf_process_stream << ", ";
      }

      nf_process_stream << "\n";
    }

    lvl--;
    pad(nf_process_stream);
    nf_process_stream << "};\n";

    nf_process_stream << "\n";

    for (auto i = 0u; i < table.n_params; i++) {
      std::stringstream param_name;
      param_name << "param_" << i;

      pad(nf_process_stream);
      nf_process_stream << "string_t " << param_name.str() << "_name";
      nf_process_stream << " = { ";
      nf_process_stream << ".str = \"" << param_name.str() << "\"";
      nf_process_stream << ", .sz = " << param_name.str().size();
      nf_process_stream << " };\n";

      for (auto byte = 0u; byte < value->getWidth() / 8; byte++) {
        auto extracted = BDD::solver_toolbox.exprBuilder->Extract(
            value, byte * 8, klee::Expr::Int8);

        auto extracted_transpiled = transpile(extracted, stack);

        pad(nf_process_stream);
        nf_process_stream << "data_buffer[data_buffer_offset+" << byte << "]";
        nf_process_stream << " = ";
        nf_process_stream << "(char) (";
        nf_process_stream << extracted_transpiled;
        nf_process_stream << ");\n";
      }

      pad(nf_process_stream);
      nf_process_stream << "string_t " << param_name.str() << "_value";
      nf_process_stream << " = { ";
      nf_process_stream << ".str = data_buffer + data_buffer_offset";
      nf_process_stream << ", .sz = " << value->getWidth() / 8;
      nf_process_stream << " };\n";

      pad(nf_process_stream);
      nf_process_stream << "data_buffer_offset += " << value->getWidth() / 8
                        << ";\n";

      pad(nf_process_stream);
      nf_process_stream << "assert(data_buffer_offset <= DATA_BUFFER_SZ);\n";
    }

    nf_process_stream << "\n";

    std::stringstream action_name;
    action_name << table.name;
    action_name << "_populate";

    pad(nf_process_stream);
    nf_process_stream << "string_t action_name";
    nf_process_stream << " = { ";
    nf_process_stream << ".str = \"" << action_name.str() << "\"";
    nf_process_stream << ", .sz = " << action_name.str().size();
    nf_process_stream << " };\n";

    pad(nf_process_stream);
    nf_process_stream << "pair_t action_params[" << table.n_params << "] = {\n";
    lvl++;

    for (auto i = 0u; i < table.n_params; i++) {
      std::stringstream param_name;
      param_name << "param_" << i;

      pad(nf_process_stream);

      nf_process_stream << "{ ";
      nf_process_stream << ".left = (void*) &" << param_name.str() << "_name";
      nf_process_stream << ", .right = (void*) &" << param_name.str()
                        << "_value";
      nf_process_stream << " }";

      if (i != table.n_params - 1) {
        nf_process_stream << ", ";
      }

      nf_process_stream << "\n";
    }

    lvl--;
    pad(nf_process_stream);
    nf_process_stream << "};\n";

    nf_process_stream << "\n";

    auto expiration_time = get_expiration_time(libvig_obj);

    pad(nf_process_stream);
    nf_process_stream << "synapse_environment_queue_insert_table_entry(";
    nf_process_stream << "table_name";
    nf_process_stream << ", keys";
    nf_process_stream << ", " << key->getWidth() / 8;
    nf_process_stream << ", action_name";
    nf_process_stream << ", action_params";
    nf_process_stream << ", " << table.n_params;
    nf_process_stream << ", 1";
    nf_process_stream << ", ";

    if (expiration_time.first) {
      nf_process_stream << expiration_time.second;
    } else {
      nf_process_stream << "0";
    }

    nf_process_stream << ");\n";

    lvl--;
    pad(nf_process_stream);
    nf_process_stream << "}\n";
  }
}

void x86_Generator::visit(const targets::x86::MapPut *node) {
  auto map_addr = node->get_map_addr();
  auto key_addr = node->get_key_addr();
  auto key = node->get_key();
  auto value = node->get_value();

  assert(!map_addr.isNull());
  assert(!key_addr.isNull());
  assert(!key.isNull());
  assert(!value.isNull());

  auto map = stack.get_label(map_addr);
  if (!map.size()) {
    stack.err_dump();
    assert(false && "Not found in stack");
  }

  auto key_label = stack.get_by_value(key);

  if (key_label.size() == 0) {
    std::cerr << "key " << expr_to_string(key, true) << "\n";
    stack.err_dump();
    exit(1);
  }

  auto transpiled_value = transpile(value, stack);

  pad(nf_process_stream);
  nf_process_stream << "map_put(";
  nf_process_stream << map;
  nf_process_stream << ", (void*) &" << key_label;
  nf_process_stream << ", " << transpiled_value;
  nf_process_stream << ");\n";

  if (is_controller.first &&
      is_controller.second == Target::BMv2SimpleSwitchgRPC) {
    issue_write_to_switch(map_addr, key, value);
  }
}

void x86_Generator::visit(const targets::x86::PacketGetUnreadLength *node) {
  auto p_addr = node->get_p_addr();
  auto unread_length = node->get_unread_length();
  auto generated_symbols = node->get_generated_symbols();

  assert(!p_addr.isNull());
  assert(!unread_length.isNull());

  auto p_label = "*p";
  stack.set_addr(p_label, p_addr);

  assert(generated_symbols.size() == 1);
  auto unread_len_label = get_label(generated_symbols, "unread_len");

  pad(nf_process_stream);
  nf_process_stream << "uint32_t " << unread_len_label;
  nf_process_stream << " = ";
  nf_process_stream << "packet_get_unread_length(";
  nf_process_stream << p_label;
  nf_process_stream << ");\n";
}

void x86_Generator::visit(const targets::x86::SetIpv4UdpTcpChecksum *node) {
  auto ip_header_addr = node->get_ip_header_addr();
  auto l4_header_addr = node->get_l4_header_addr();
  auto p_addr = node->get_p_addr();
  auto generated_symbols = node->get_generated_symbols();

  assert(!ip_header_addr.isNull());
  assert(!l4_header_addr.isNull());
  assert(!p_addr.isNull());

  auto ip_header_label = stack.get_label(ip_header_addr);
  auto l4_header_label = stack.get_label(l4_header_addr);

  stack.set_value("p", p_addr);

  assert(generated_symbols.size() == 1);
  auto checksum_label = get_label(generated_symbols, "checksum");
  auto ip_hdr = stack.get_value(ip_header_addr);
  auto checksum_expr = BDD::solver_toolbox.exprBuilder->Extract(ip_hdr, 80, 16);

  pad(nf_process_stream);
  nf_process_stream << "uint16_t " << checksum_label;
  nf_process_stream << " = " << transpile(checksum_expr, stack);
  nf_process_stream << ";\n";

  stack.add(checksum_label, checksum_expr);

  pad(nf_process_stream);
  nf_process_stream << "nf_set_rte_ipv4_udptcp_checksum(";
  nf_process_stream << "(struct rte_ipv4_hdr *) " << ip_header_label;
  nf_process_stream << ", (struct tcpudp_hdr *) " << l4_header_label;
  nf_process_stream << ", *p";
  nf_process_stream << ");\n";
}

void x86_Generator::visit(const targets::x86::DchainIsIndexAllocated *node) {
  auto dchain_addr = node->get_dchain_addr();
  auto index = node->get_index();
  auto is_allocated = node->get_is_allocated();

  auto generated_symbols = node->get_generated_symbols();

  assert(!dchain_addr.isNull());
  assert(!index.isNull());
  assert(!is_allocated.isNull());

  auto dchain = stack.get_label(dchain_addr);
  if (!dchain.size()) {
    stack.err_dump();
    assert(false && "Not found in stack");
  }

  assert(generated_symbols.size() == 1);

  auto is_allocated_label =
      get_label(generated_symbols, "dchain_is_index_allocated");

  stack.add(is_allocated_label, is_allocated);

  pad(nf_process_stream);
  nf_process_stream << "int " << is_allocated_label;
  nf_process_stream << " = ";
  nf_process_stream << "dchain_is_index_allocated(";
  nf_process_stream << dchain;
  nf_process_stream << ", " << transpile(index, stack);
  nf_process_stream << ");\n";
}

} // namespace synapse