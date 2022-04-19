#include "printer.h"

#include <algorithm>
#include <regex>

bool get_bytes_read(klee::ref<klee::Expr> expr, std::vector<unsigned> &bytes) {
  switch (expr->getKind()) {
  case klee::Expr::Kind::Read: {
    klee::ReadExpr *read = dyn_cast<klee::ReadExpr>(expr);
    auto index = read->index;

    if (index->getKind() != klee::Expr::Kind::Constant) {
      return false;
    }

    klee::ConstantExpr *index_const =
        static_cast<klee::ConstantExpr *>(index.get());
    bytes.push_back(index_const->getZExtValue());

    return true;
  };
  case klee::Expr::Kind::Concat: {
    klee::ConcatExpr *concat = dyn_cast<klee::ConcatExpr>(expr);

    auto left = concat->getLeft();
    auto right = concat->getRight();

    if (!get_bytes_read(left, bytes))
      return false;
    if (!get_bytes_read(right, bytes))
      return false;

    return true;
  };
  default: {
  }
  }

  return false;
}

bool is_readLSB_complete(klee::ref<klee::Expr> expr) {
  auto size = expr->getWidth();
  assert(size % 8 == 0);
  size /= 8;

  RetrieveSymbols retriever;
  retriever.visit(expr);

  if (retriever.get_retrieved_strings().size() > 1) {
    return false;
  }

  std::vector<unsigned> bytes_read;
  if (!get_bytes_read(expr, bytes_read)) {
    return false;
  }

  unsigned expected_byte = size - 1;
  for (const auto &byte : bytes_read) {
    if (byte != expected_byte) {
      return false;
    }

    expected_byte -= 1;
  }

  return true;
}

class ExprPrettyPrinter : public klee::ExprVisitor::ExprVisitor {
private:
  std::string result;
  bool use_signed;

public:
  ExprPrettyPrinter(bool _use_signed) : ExprVisitor(false) {
    use_signed = _use_signed;
  }

  ExprPrettyPrinter() : ExprPrettyPrinter(false) {}

  static std::string print(klee::ref<klee::Expr> expr,
                           bool use_signed = false) {
    assert(!expr.isNull());

    if (expr->getKind() != klee::Expr::Kind::Constant) {
      ExprPrettyPrinter printer(use_signed);
      printer.visit(expr);
      return printer.get_result();
    }

    klee::ConstantExpr *constant =
        static_cast<klee::ConstantExpr *>(expr.get());
    std::stringstream ss;

    if (use_signed) {
      switch (constant->getWidth()) {
      case klee::Expr::Bool: {
        bool value = constant->getZExtValue(1);
        ss << value;
        break;
      };
      case klee::Expr::Int8: {
        int8_t value = constant->getZExtValue(8);
        ss << value;
        break;
      };
      case klee::Expr::Int16: {
        int16_t value = constant->getZExtValue(16);
        ss << value;
        break;
      };
      case klee::Expr::Int32: {
        int32_t value = constant->getZExtValue(32);
        ss << value;
        break;
      };
      case klee::Expr::Int64: {
        int64_t value = constant->getZExtValue(64);
        ss << value;
        break;
      };
      default: {
        ss << expr_to_string(expr, true);
        break;
      };
      }
    } else {
      if (constant->getWidth() <= 64) {
        uint64_t value = constant->getZExtValue(constant->getWidth());
        ss << value;
      } else {
        ss << expr_to_string(expr, true);
      }
    }

    return ss.str();
  }

  const std::string &get_result() const { return result; }

  klee::ExprVisitor::Action visitRead(const klee::ReadExpr &e) {
    auto ul = e.updates;
    auto root = ul.root;
    auto index = e.index;

    assert(index->getKind() == klee::Expr::Kind::Constant);

    klee::ConstantExpr *index_const =
        static_cast<klee::ConstantExpr *>(index.get());
    auto i = index_const->getZExtValue();

    std::stringstream ss;
    ss << root->name;
    ss << "[" << i << "]";
    result = ss.str();

    return klee::ExprVisitor::Action::skipChildren();
  }

  klee::ExprVisitor::Action visitSelect(const klee::SelectExpr &e) {
    std::stringstream ss;

    auto cond = ExprPrettyPrinter::print(e.getKid(0), use_signed);
    auto first = ExprPrettyPrinter::print(e.getKid(1), use_signed);
    auto second = ExprPrettyPrinter::print(e.getKid(2), use_signed);

    ss << cond << " ? " << first << " : " << second;
    result = ss.str();

    return klee::ExprVisitor::Action::skipChildren();
  }

  klee::ExprVisitor::Action visitConcat(const klee::ConcatExpr &e) {
    klee::ref<klee::Expr> eref = const_cast<klee::ConcatExpr *>(&e);
    std::stringstream ss;

    RetrieveSymbols retriever;
    retriever.visit(eref);
    auto retrieved_strs = retriever.get_retrieved_strings();

    assert(retrieved_strs.size());
    auto symbol = *retrieved_strs.begin();

    if (is_readLSB_complete(eref)) {
      result = symbol;
      return klee::ExprVisitor::Action::skipChildren();
    }

    std::vector<unsigned> bytes_read;
    auto success = get_bytes_read(eref, bytes_read);
    std::sort(bytes_read.begin(), bytes_read.end());

    if (success && bytes_read.size() && retrieved_strs.size() == 1) {
      bool processed = false;
      auto lower = bytes_read[0];
      auto higher = lower;

      for (unsigned ibyte = 1; ibyte < bytes_read.size(); ibyte++) {
        auto byte = bytes_read[ibyte];

        if (byte == higher + 1) {
          higher = byte;
          processed = false;
          continue;
        }

        if (ss.str().size()) {
          ss << "|||";
        }

        ss << symbol;

        if (lower == higher) {
          ss << "[" << lower << "]";
        } else {
          ss << "[" << lower << ":" << higher << "]";
        }

        lower = byte;
        higher = byte;

        processed = true;
      }

      if (!processed) {
        if (ss.str().size()) {
          ss << "|||";
        }

        ss << symbol;

        if (lower == higher) {
          ss << "[" << lower << "]";
        } else {
          ss << "[" << lower << ":" << higher << "]";
        }
      }

      result = ss.str();
      return klee::ExprVisitor::Action::skipChildren();
    }

    auto left = e.getLeft();
    auto right = e.getRight();

    assert(!left.isNull());
    assert(!right.isNull());

    ss << "(";
    ss << pretty_print_expr(left);
    ss << ")";
    ss << "|||";
    ss << "(";
    ss << pretty_print_expr(right);
    ss << ")";

    result = ss.str();
    return klee::ExprVisitor::Action::skipChildren();
  }

  klee::ExprVisitor::Action visitExtract(const klee::ExtractExpr &e) {
    auto expr = e.getKid(0);
    auto offset_value = e.offset;

    auto arg = ExprPrettyPrinter::print(expr, use_signed);

    if (offset_value == 0) {
      result = arg;
      return klee::ExprVisitor::Action::skipChildren();
    }

    std::stringstream ss;
    ss << "(Extract " << offset_value << " " << arg << " )";
    result = ss.str();

    return klee::ExprVisitor::Action::skipChildren();
  }

  klee::ExprVisitor::Action visitZExt(const klee::ZExtExpr &e) {
    result = ExprPrettyPrinter::print(e.getKid(0), use_signed);
    return klee::ExprVisitor::Action::skipChildren();
  }

  klee::ExprVisitor::Action visitSExt(const klee::SExtExpr &e) {
    result = ExprPrettyPrinter::print(e.getKid(0), true);
    return klee::ExprVisitor::Action::skipChildren();
  }

  klee::ExprVisitor::Action visitAdd(const klee::AddExpr &e) {
    std::stringstream ss;

    auto left = ExprPrettyPrinter::print(e.getKid(0), use_signed);
    auto right = ExprPrettyPrinter::print(e.getKid(1), use_signed);

    ss << "(" << left << " + " << right << ")";
    result = ss.str();

    return klee::ExprVisitor::Action::skipChildren();
  }

  klee::ExprVisitor::Action visitSub(const klee::SubExpr &e) {
    std::stringstream ss;

    auto left = ExprPrettyPrinter::print(e.getKid(0), use_signed);
    auto right = ExprPrettyPrinter::print(e.getKid(1), use_signed);

    ss << "(" << left << " - " << right << ")";
    result = ss.str();

    return klee::ExprVisitor::Action::skipChildren();
  }

  klee::ExprVisitor::Action visitMul(const klee::MulExpr &e) {
    std::stringstream ss;

    auto left = ExprPrettyPrinter::print(e.getKid(0), use_signed);
    auto right = ExprPrettyPrinter::print(e.getKid(1), use_signed);

    ss << "(" << left << " * " << right << ")";
    result = ss.str();

    return klee::ExprVisitor::Action::skipChildren();
  }

  klee::ExprVisitor::Action visitUDiv(const klee::UDivExpr &e) {
    std::stringstream ss;

    auto left = ExprPrettyPrinter::print(e.getKid(0), use_signed);
    auto right = ExprPrettyPrinter::print(e.getKid(1), use_signed);

    ss << "(" << left << " / " << right << ")";
    result = ss.str();

    return klee::ExprVisitor::Action::skipChildren();
  }

  klee::ExprVisitor::Action visitSDiv(const klee::SDivExpr &e) {
    std::stringstream ss;

    auto left = ExprPrettyPrinter::print(e.getKid(0), true);
    auto right = ExprPrettyPrinter::print(e.getKid(1), true);

    ss << "(" << left << " + " << right << ")";
    result = ss.str();

    return klee::ExprVisitor::Action::skipChildren();
  }

  klee::ExprVisitor::Action visitURem(const klee::URemExpr &e) {
    std::stringstream ss;

    auto left = ExprPrettyPrinter::print(e.getKid(0), use_signed);
    auto right = ExprPrettyPrinter::print(e.getKid(1), use_signed);

    ss << "(" << left << " % " << right << ")";
    result = ss.str();

    return klee::ExprVisitor::Action::skipChildren();
  }

  klee::ExprVisitor::Action visitSRem(const klee::SRemExpr &e) {
    std::stringstream ss;

    auto left = ExprPrettyPrinter::print(e.getKid(0), true);
    auto right = ExprPrettyPrinter::print(e.getKid(1), true);

    ss << "(" << left << " % " << right << ")";
    result = ss.str();

    return klee::ExprVisitor::Action::skipChildren();
  }

  klee::ExprVisitor::Action visitNot(const klee::NotExpr &e) {
    std::stringstream ss;

    auto arg = ExprPrettyPrinter::print(e.getKid(0), use_signed);

    ss << "!" << arg;
    result = ss.str();

    return klee::ExprVisitor::Action::skipChildren();
  }

  klee::ExprVisitor::Action visitAnd(const klee::AndExpr &e) {
    std::stringstream ss;

    auto left = ExprPrettyPrinter::print(e.getKid(0), use_signed);
    auto right = ExprPrettyPrinter::print(e.getKid(1), use_signed);

    ss << "(" << left << " & " << right << ")";
    result = ss.str();

    return klee::ExprVisitor::Action::skipChildren();
  }

  klee::ExprVisitor::Action visitOr(const klee::OrExpr &e) {
    std::stringstream ss;

    auto left = ExprPrettyPrinter::print(e.getKid(0), use_signed);
    auto right = ExprPrettyPrinter::print(e.getKid(1), use_signed);

    ss << "(" << left << " | " << right << ")";
    result = ss.str();

    return klee::ExprVisitor::Action::skipChildren();
  }

  klee::ExprVisitor::Action visitXor(const klee::XorExpr &e) {
    std::stringstream ss;

    auto left = ExprPrettyPrinter::print(e.getKid(0), use_signed);
    auto right = ExprPrettyPrinter::print(e.getKid(1), use_signed);

    ss << "(" << left << " ^ " << right << ")";
    result = ss.str();

    return klee::ExprVisitor::Action::skipChildren();
  }

  klee::ExprVisitor::Action visitShl(const klee::ShlExpr &e) {
    std::stringstream ss;

    auto left = ExprPrettyPrinter::print(e.getKid(0), use_signed);
    auto right = ExprPrettyPrinter::print(e.getKid(1), use_signed);

    ss << "(" << left << " << " << right << ")";
    result = ss.str();

    return klee::ExprVisitor::Action::skipChildren();
  }

  klee::ExprVisitor::Action visitLShr(const klee::LShrExpr &e) {
    std::stringstream ss;

    auto left = ExprPrettyPrinter::print(e.getKid(0), use_signed);
    auto right = ExprPrettyPrinter::print(e.getKid(1), use_signed);

    ss << "(" << left << " >> " << right << ")";
    result = ss.str();

    return klee::ExprVisitor::Action::skipChildren();
  }

  klee::ExprVisitor::Action visitAShr(const klee::AShrExpr &e) {
    std::stringstream ss;

    auto left = ExprPrettyPrinter::print(e.getKid(0), true);
    auto right = ExprPrettyPrinter::print(e.getKid(1), true);

    ss << "(" << left << " >> " << right << ")";
    result = ss.str();

    return klee::ExprVisitor::Action::skipChildren();
  }

  klee::ExprVisitor::Action visitEq(const klee::EqExpr &e) {
    std::stringstream ss;

    auto left = ExprPrettyPrinter::print(e.getKid(0), use_signed);
    auto right = ExprPrettyPrinter::print(e.getKid(1), use_signed);

    auto p0 = std::regex(R"(\(0 == (.+)\))");
    auto p1 = std::regex(R"(!(.+))");
    std::smatch m;

    if (left == "0" &&
        (std::regex_match(right, m, p0) || std::regex_match(right, m, p1))) {
      ss << m.str(1);
    }

    else if (left == "0") {
      ss << "!" << right;
    }

    else {
      ss << "(" << left << " == " << right << ")";
    }

    result = ss.str();
    return klee::ExprVisitor::Action::skipChildren();
  }

  klee::ExprVisitor::Action visitNe(const klee::NeExpr &e) {
    std::stringstream ss;

    auto left = ExprPrettyPrinter::print(e.getKid(0), use_signed);
    auto right = ExprPrettyPrinter::print(e.getKid(1), use_signed);

    ss << "(" << left << " != " << right << ")";
    result = ss.str();

    return klee::ExprVisitor::Action::skipChildren();
  }

  klee::ExprVisitor::Action visitUlt(const klee::UltExpr &e) {
    std::stringstream ss;

    auto left = ExprPrettyPrinter::print(e.getKid(0), use_signed);
    auto right = ExprPrettyPrinter::print(e.getKid(1), use_signed);

    ss << "(" << left << " < " << right << ")";
    result = ss.str();

    return klee::ExprVisitor::Action::skipChildren();
  }

  klee::ExprVisitor::Action visitUle(const klee::UleExpr &e) {
    std::stringstream ss;

    auto left = ExprPrettyPrinter::print(e.getKid(0), use_signed);
    auto right = ExprPrettyPrinter::print(e.getKid(1), use_signed);

    ss << "(" << left << " <= " << right << ")";
    result = ss.str();

    return klee::ExprVisitor::Action::skipChildren();
  }

  klee::ExprVisitor::Action visitUgt(const klee::UgtExpr &e) {
    std::stringstream ss;

    auto left = ExprPrettyPrinter::print(e.getKid(0), use_signed);
    auto right = ExprPrettyPrinter::print(e.getKid(1), use_signed);

    ss << "(" << left << " > " << right << ")";
    result = ss.str();

    return klee::ExprVisitor::Action::skipChildren();
  }

  klee::ExprVisitor::Action visitUge(const klee::UgeExpr &e) {
    std::stringstream ss;

    auto left = ExprPrettyPrinter::print(e.getKid(0), use_signed);
    auto right = ExprPrettyPrinter::print(e.getKid(1), use_signed);

    ss << "(" << left << " >= " << right << ")";
    result = ss.str();

    return klee::ExprVisitor::Action::skipChildren();
  }

  klee::ExprVisitor::Action visitSlt(const klee::SltExpr &e) {
    std::stringstream ss;

    auto left = ExprPrettyPrinter::print(e.getKid(0), true);
    auto right = ExprPrettyPrinter::print(e.getKid(1), true);

    ss << "(" << left << " < " << right << ")";
    result = ss.str();

    return klee::ExprVisitor::Action::skipChildren();
  }

  klee::ExprVisitor::Action visitSle(const klee::SleExpr &e) {
    std::stringstream ss;

    auto left = ExprPrettyPrinter::print(e.getKid(0), true);
    auto right = ExprPrettyPrinter::print(e.getKid(1), true);

    ss << "(" << left << " <= " << right << ")";
    result = ss.str();

    return klee::ExprVisitor::Action::skipChildren();
  }

  klee::ExprVisitor::Action visitSgt(const klee::SgtExpr &e) {
    std::stringstream ss;

    auto left = ExprPrettyPrinter::print(e.getKid(0), true);
    auto right = ExprPrettyPrinter::print(e.getKid(1), true);

    ss << "(" << left << " > " << right << ")";
    result = ss.str();

    return klee::ExprVisitor::Action::skipChildren();
  }

  klee::ExprVisitor::Action visitSge(const klee::SgeExpr &e) {
    std::stringstream ss;

    auto left = ExprPrettyPrinter::print(e.getKid(0), true);
    auto right = ExprPrettyPrinter::print(e.getKid(1), true);

    ss << "(" << left << " >= " << right << ")";
    result = ss.str();

    return klee::ExprVisitor::Action::skipChildren();
  }
};

std::string pretty_print_expr(klee::ref<klee::Expr> expr) {
  auto printed = ExprPrettyPrinter::print(expr);
  return printed;
}

std::string expr_to_string(klee::ref<klee::Expr> expr, bool one_liner) {
  std::string expr_str;
  if (expr.isNull())
    return expr_str;
  llvm::raw_string_ostream os(expr_str);
  expr->print(os);
  os.str();

  if (one_liner) {
    // remove new lines
    expr_str.erase(std::remove(expr_str.begin(), expr_str.end(), '\n'),
                   expr_str.end());

    // remove duplicated whitespaces
    auto bothAreSpaces = [](char lhs, char rhs) -> bool {
      return (lhs == rhs) && (lhs == ' ');
    };
    std::string::iterator new_end =
        std::unique(expr_str.begin(), expr_str.end(), bothAreSpaces);
    expr_str.erase(new_end, expr_str.end());
  }

  return expr_str;
}
