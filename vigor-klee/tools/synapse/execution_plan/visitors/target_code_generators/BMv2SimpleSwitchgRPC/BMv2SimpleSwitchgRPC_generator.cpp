#include "BMv2SimpleSwitchgRPC_generator.h"

#include "../../../../log.h"
#include "../../../../modules/modules.h"

#include "klee_expr_to_p4.h"

#define MARKER_HEADERS_DEFINITIONS "headers_definitions"
#define MARKER_HEADERS_DECLARATIONS "headers_declarations"
#define MARKER_METADATA_FIELDS "metadata_fields"
#define MARKER_PARSE_HEADERS "parse_headers"
#define MARKER_INGRESS_GLOBALS "ingress_globals"
#define MARKER_INGRESS_APPLY_CONTENT "ingress_apply_content"
#define MARKER_INGRESS_TAG_VERSIONS_ACTIONS "ingress_tag_versions_action"
#define MARKER_DEPARSER_APPLY "deparser_apply"

namespace synapse {

void BMv2SimpleSwitchgRPC_Generator::err_label_from_chunk(
    klee::ref<klee::Expr> expr) const {
  Log::err() << "label_from_chunk error\n";
  Log::err() << "expr   " << expr_to_string(expr, true) << "\n";
  for (auto header : headers) {
    Log::err() << "header " << header.label << " "
               << expr_to_string(header.chunk, true) << "\n";
  }
  Log::err() << "\n";

  assert(false);
}

void BMv2SimpleSwitchgRPC_Generator::err_label_from_vars(
    klee::ref<klee::Expr> expr) const {
  Log::err() << "label_from_vars error\n";
  Log::err() << "expr   " << expr_to_string(expr, true) << "\n";

  for (auto meta : metadata.get()) {
    std::stringstream meta_stream;
    meta_stream << "meta   " << meta.label << " ";
    for (auto expr : meta.exprs) {
      meta_stream << expr_to_string(expr, true) << " ";
    }
    meta_stream << "\n";
    Log::err() << meta_stream.str();
  }

  Log::err() << "\n";
  for (auto local_var : local_vars.get()) {
    Log::err() << "var    " << local_var.label << " " << local_var.symbol
               << "\n";
  }

  Log::err() << "\n";
  assert(false);
}

std::string BMv2SimpleSwitchgRPC_Generator::p4_type_from_expr(
    klee::ref<klee::Expr> expr) const {
  auto sz = expr->getWidth();
  std::stringstream label;
  label << "bit<" << sz << ">";
  return label.str();
}

std::string get_bytes_of_label(std::string label, unsigned size,
                               unsigned offset) {
  std::stringstream code;

  uint64_t mask = 0;
  for (unsigned b = 0; b < size; b++) {
    mask <<= 1;
    mask |= 1;
  }

  assert(mask > 0);

  if (offset > 0) {
    code << "(";
  }

  code << label;
  if (offset > 0) {
    code << " >> " << offset << ")";
  }

  code << std::hex;
  code << " & 0x" << mask;
  code << std::dec;

  return code.str();
}

void BMv2SimpleSwitchgRPC_Generator::field_header_from_packet_chunk(
    klee::ref<klee::Expr> expr, std::string &field_str,
    unsigned &bit_offset) const {
  RetrieveSymbols retriever;
  retriever.visit(expr);

  auto symbols = retriever.get_retrieved_strings();

  if (symbols.size() != 1 || (*symbols.begin()) != "packet_chunks") {
    return;
  }

  auto sz = expr->getWidth();

  for (auto header : headers) {
    auto chunk = header.chunk;
    auto offset = 0u;

    for (auto field : header.fields) {

      for (unsigned byte = 0; byte * 8 + sz <= field.sz; byte++) {
        auto field_expr = BDD::solver_toolbox.exprBuilder->Extract(
            chunk, offset + byte * 8, sz);

        if (BDD::solver_toolbox.are_exprs_always_equal(field_expr, expr)) {
          field_str = std::string("hdr." + header.label + "." + field.label);
          bit_offset = byte * 8;
          return;
        }
      }

      offset += field.sz;
    }
  }

  return;
}

std::string BMv2SimpleSwitchgRPC_Generator::label_from_packet_chunk(
    klee::ref<klee::Expr> expr) const {
  RetrieveSymbols retriever;
  retriever.visit(expr);

  auto symbols = retriever.get_retrieved_strings();

  if (symbols.size() != 1 || (*symbols.begin()) != "packet_chunks") {
    return "";
  }

  auto sz = expr->getWidth();

  for (auto header : headers) {
    auto chunk = header.chunk;
    auto offset = 0u;

    for (auto field : header.fields) {
      for (unsigned byte = 0; byte * 8 + sz <= field.sz; byte++) {
        if (offset + byte * 8 + sz > chunk->getWidth()) {
          continue;
        }

        auto field_expr = BDD::solver_toolbox.exprBuilder->Extract(
            chunk, offset + byte * 8, sz);

        if (BDD::solver_toolbox.are_exprs_always_equal(field_expr, expr)) {
          auto label = "hdr." + header.label + "." + field.label;

          if (field.sz == sz) {
            return label;
          }

          return get_bytes_of_label(label, sz, byte * 8);
        }
      }

      offset += field.sz;
    }
  }

  return "";
}

std::string BMv2SimpleSwitchgRPC_Generator::label_from_vars(
    klee::ref<klee::Expr> expr) const {
  RetrieveSymbols retriever;
  retriever.visit(expr);

  auto symbols = retriever.get_retrieved_strings();
  assert(symbols.size() == 1);

  auto sz = expr->getWidth();

  for (auto meta : metadata.get()) {
    for (auto meta_expr : meta.exprs) {
      auto meta_sz = meta_expr->getWidth();

      for (auto byte = 0u; byte * 8 + sz <= meta_sz; byte++) {
        auto extracted =
            BDD::solver_toolbox.exprBuilder->Extract(meta_expr, byte * 8, sz);

        if (BDD::solver_toolbox.are_exprs_always_equal(expr, extracted)) {
          auto label = "meta." + meta.label;

          if (meta_sz == sz) {
            return label;
          }

          return get_bytes_of_label(label, sz, byte * 8);
        }
      }
    }
  }

  for (auto local_var : local_vars.get()) {
    auto local_var_vigor_symbol = local_var.symbol;
    auto symbol = *symbols.begin();

    if (symbol == local_var_vigor_symbol) {
      return local_var.label;
    }
  }

  return "";
}

std::vector<std::string>
BMv2SimpleSwitchgRPC_Generator::assign_key_bytes(klee::ref<klee::Expr> expr) {
  std::vector<std::string> assignments;
  auto sz = expr->getWidth();

  for (auto byte = 0u; byte * 8 < sz; byte++) {
    auto key_byte = BDD::solver_toolbox.exprBuilder->Extract(expr, byte * 8, 8);
    auto key_byte_code = transpile(key_byte, true);

    if (byte + 1 > ingress.key_bytes.size()) {
      std::stringstream label;
      label << "key_byte_" << byte;

      ingress.key_bytes.emplace_back(label.str(), 8);
    }

    auto key_byte_declaration = ingress.key_bytes[byte];

    std::stringstream assignment;
    assignment << key_byte_declaration.label;
    assignment << " = (bit<8>) (" << key_byte_code << ")";

    assignments.push_back(assignment.str());
  }

  return assignments;
}

bool BMv2SimpleSwitchgRPC_Generator::is_constant(
    klee::ref<klee::Expr> expr) const {
  if (expr->getKind() == klee::Expr::Kind::Constant) {
    return true;
  }

  return false;
}

bool BMv2SimpleSwitchgRPC_Generator::is_constant_signed(
    klee::ref<klee::Expr> expr) const {
  if (!is_constant(expr)) {
    return false;
  }

  auto constant = static_cast<klee::ConstantExpr *>(expr.get());
  assert(constant->getWidth() <= 64);

  auto value = constant->getZExtValue(constant->getWidth());
  auto sign_bit = value >> (constant->getWidth() - 1);

  return sign_bit == 1;
}

int64_t BMv2SimpleSwitchgRPC_Generator::get_constant_signed(
    klee::ref<klee::Expr> expr) const {
  if (!is_constant_signed(expr)) {
    return false;
  }

  auto constant = static_cast<klee::ConstantExpr *>(expr.get());
  assert(constant->getWidth() <= 64);
  auto value = constant->getZExtValue(constant->getWidth());

  uint64_t mask = 0;
  for (uint64_t i = 0u; i < constant->getWidth(); i++) {
    mask <<= 1;
    mask |= 1;
  }

  return -((~value + 1) & mask);
}

std::string
BMv2SimpleSwitchgRPC_Generator::transpile(const klee::ref<klee::Expr> &e,
                                          bool is_signed) const {
  auto expr = e;
  KleeExprToP4::swap_endianness(expr);

  if (is_constant(expr)) {
    std::stringstream ss;
    auto constant = static_cast<klee::ConstantExpr *>(expr.get());
    assert(constant->getWidth() <= 64);

    if (is_signed) {
      assert(!is_constant_signed(expr) &&
             "Be careful with negative numbers...");
    }

    ss << "(";
    ss << "(bit<";
    ss << constant->getWidth();
    ss << ">) ";
    ss << constant->getZExtValue();
    ss << ")";

    return ss.str();
  }

  KleeExprToP4 kleeExprToP4(*this, is_signed);
  kleeExprToP4.visit(expr);

  auto code = kleeExprToP4.get_code();

  if (!code.size()) {
    // error
    std::stringstream error;
    error << "Unable to generator.transpile expression: ";
    error << expr_to_string(expr, true);
    error << "\n";
    error << "Kind: ";
    error << expr->getKind();
    error << "\n";

    Log::err() << error.str();
    exit(1);
  }

  return code;
}

void BMv2SimpleSwitchgRPC_Generator::parser_t::dump(
    code_builder_t &code_builder) {
  assert(stages.size());
  auto root = stages[0];

  std::stringstream parser_states_stream;
  auto lvl = code_builder.get_indentation_level(MARKER_PARSE_HEADERS);

  pad(parser_states_stream, lvl);
  parser_states_stream << "state parse_headers {\n";

  lvl++;
  pad(parser_states_stream, lvl);
  parser_states_stream << "transition ";
  parser_states_stream << root->label;
  parser_states_stream << ";\n";
  lvl--;

  pad(parser_states_stream, lvl);
  parser_states_stream << "}\n";

  auto built_stages = std::vector<std::shared_ptr<parsing_stage>>{root};

  while (built_stages.size()) {
    auto stage = built_stages[0];
    built_stages.erase(built_stages.begin());

    if (stage->type == TERMINATOR) {
      continue;
    }

    parser_states_stream << "\n";

    pad(parser_states_stream, lvl);
    parser_states_stream << "state ";
    parser_states_stream << stage->label << " {\n";
    lvl++;

    if (stage->type == CONDITIONAL) {
      auto conditional = static_cast<conditional_stage *>(stage.get());

      pad(parser_states_stream, lvl);
      parser_states_stream << "transition select(";
      parser_states_stream << conditional->condition;
      parser_states_stream << ") {\n";

      lvl++;

      pad(parser_states_stream, lvl);
      parser_states_stream << "true: ";
      if (conditional->next_on_true) {
        parser_states_stream << conditional->next_on_true->label;
        built_stages.push_back(conditional->next_on_false);
      }
      parser_states_stream << ";\n";

      if (conditional->next_on_false) {
        if (conditional->next_on_false->label != "reject") {
          pad(parser_states_stream, lvl);
          parser_states_stream << "false: ";
          parser_states_stream << conditional->next_on_false->label;
          parser_states_stream << ";\n";
        }

        built_stages.push_back(conditional->next_on_true);
      }

      lvl--;

      pad(parser_states_stream, lvl);
      parser_states_stream << "}\n";

    } else if (stage->type == EXTRACTOR) {
      auto extractor = static_cast<extractor_stage *>(stage.get());
      assert(extractor->next);

      pad(parser_states_stream, lvl);
      parser_states_stream << "packet.extract(hdr." << extractor->hdr;

      if (extractor->dynamic_length.size()) {
        parser_states_stream << ", " << extractor->dynamic_length;
      }

      parser_states_stream << ");\n";

      pad(parser_states_stream, lvl);
      parser_states_stream << "transition ";
      parser_states_stream << extractor->next->label;
      parser_states_stream << ";\n";

      built_stages.push_back(extractor->next);
    }

    lvl--;
    pad(parser_states_stream, lvl);
    parser_states_stream << "}\n";
  }

  code_builder.fill_mark(MARKER_PARSE_HEADERS, parser_states_stream.str());
}

void BMv2SimpleSwitchgRPC_Generator::verify_checksum_t::dump(
    code_builder_t &code_builder) {}

void BMv2SimpleSwitchgRPC_Generator::ingress_t::dump(
    code_builder_t &code_builder) {
  std::stringstream ingress_globals_stream;
  auto lvl = code_builder.get_indentation_level(MARKER_INGRESS_GLOBALS);

  for (auto key_byte : key_bytes) {
    pad(ingress_globals_stream, lvl);
    ingress_globals_stream << "bit<" << key_byte.size << "> " << key_byte.label
                           << ";\n";
  }

  std::vector<std::string> declared_table_ids;

  for (auto table : tables) {
    auto found_it = std::find(declared_table_ids.begin(),
                              declared_table_ids.end(), table.label);
    if (found_it == declared_table_ids.end()) {
      table.dump(ingress_globals_stream, 1);
      declared_table_ids.push_back(table.label);
    }
  }

  code_builder.fill_mark(MARKER_INGRESS_GLOBALS, ingress_globals_stream.str());

  std::stringstream ingress_apply_content_stream;
  lvl = code_builder.get_indentation_level(MARKER_INGRESS_APPLY_CONTENT);
  ingress_apply_content_stream << apply_block.str();
  code_builder.fill_mark(MARKER_INGRESS_APPLY_CONTENT,
                         ingress_apply_content_stream.str());
}

void BMv2SimpleSwitchgRPC_Generator::egress_t::dump(
    code_builder_t &code_builder) {}

void BMv2SimpleSwitchgRPC_Generator::compute_checksum_t::dump(
    code_builder_t &code_builder) {}

void BMv2SimpleSwitchgRPC_Generator::deparser_t::dump(
    code_builder_t &code_builder) {
  std::unordered_set<std::string> defined_hdrs;

  std::stringstream deparser_apply_stream;

  for (auto header_label : headers_labels) {
    if (defined_hdrs.find(header_label) != defined_hdrs.end()) {
      continue;
    }

    pad(deparser_apply_stream, lvl + 1);
    deparser_apply_stream << "packet.emit(hdr." << header_label << ");\n";

    defined_hdrs.insert(header_label);
  }

  code_builder.fill_mark(MARKER_DEPARSER_APPLY, deparser_apply_stream.str());
}

void BMv2SimpleSwitchgRPC_Generator::dump() {
  std::stringstream headers_definitions_stream;
  auto lvl = code_builder.get_indentation_level(MARKER_HEADERS_DEFINITIONS);

  std::unordered_set<std::string> defined_hdrs;

  for (auto header : headers) {
    if (defined_hdrs.find(header.type_label) != defined_hdrs.end()) {
      continue;
    }

    pad(headers_definitions_stream, lvl);
    headers_definitions_stream << "header " << header.type_label << " {\n";
    lvl++;

    for (auto field : header.fields) {
      pad(headers_definitions_stream, lvl);
      headers_definitions_stream << field.type << " " << field.label << ";\n";
    }

    lvl--;
    headers_definitions_stream << "}\n\n";

    defined_hdrs.insert(header.type_label);
  }

  code_builder.fill_mark(MARKER_HEADERS_DEFINITIONS,
                         headers_definitions_stream.str());

  lvl = code_builder.get_indentation_level(MARKER_HEADERS_DECLARATIONS);
  std::stringstream headers_declarations_stream;

  defined_hdrs.clear();

  for (auto header : headers) {
    if (defined_hdrs.find(header.type_label) != defined_hdrs.end()) {
      continue;
    }

    pad(headers_declarations_stream, lvl);
    headers_declarations_stream << header.type_label << " " << header.label
                                << ";\n";

    defined_hdrs.insert(header.type_label);
  }

  code_builder.fill_mark(MARKER_HEADERS_DECLARATIONS,
                         headers_declarations_stream.str());

  std::stringstream metadata_fields_stream;

  lvl = code_builder.get_indentation_level(MARKER_METADATA_FIELDS);

  std::vector<std::pair<unsigned, std::string>> meta_tags;

  for (auto meta : metadata.get_all()) {
    pad(metadata_fields_stream, lvl);

    metadata_fields_stream << "bit<" << meta.sz << "> ";
    metadata_fields_stream << meta.label << ";\n";

    auto marker = std::string("_tag");
    auto delim = meta.label.find(marker);

    if (delim != std::string::npos &&
        delim + marker.size() == meta.label.size()) {
      meta_tags.push_back(std::make_pair(meta.sz, meta.label));
    }
  }

  code_builder.fill_mark(MARKER_METADATA_FIELDS, metadata_fields_stream.str());

  if (meta_tags.size()) {
    std::stringstream ingress_tag_versions_action_stream;
    lvl =
        code_builder.get_indentation_level(MARKER_INGRESS_TAG_VERSIONS_ACTIONS);

    pad(ingress_tag_versions_action_stream, lvl);
    ingress_tag_versions_action_stream << "action tag_versions(";

    auto first = true;
    for (auto meta : meta_tags) {
      if (first) {
        first = false;
      } else {
        ingress_tag_versions_action_stream << ", ";
      }

      ingress_tag_versions_action_stream << "bit<" << meta.first << "> ";
      ingress_tag_versions_action_stream << meta.second;
    }

    ingress_tag_versions_action_stream << ") {\n";

    lvl++;

    for (auto meta : meta_tags) {
      pad(ingress_tag_versions_action_stream, lvl);

      ingress_tag_versions_action_stream << "meta." << meta.second;
      ingress_tag_versions_action_stream << " = " << meta.second;
      ingress_tag_versions_action_stream << ";\n";
    }

    lvl--;

    pad(ingress_tag_versions_action_stream, lvl);
    ingress_tag_versions_action_stream << "}\n";

    code_builder.fill_mark(MARKER_INGRESS_TAG_VERSIONS_ACTIONS,
                           ingress_tag_versions_action_stream.str());
  }

  parser.dump(code_builder);
  verify_checksum.dump(code_builder);
  ingress.dump(code_builder);
  egress.dump(code_builder);
  compute_checksum.dump(code_builder);
  deparser.dump(code_builder);
}

void BMv2SimpleSwitchgRPC_Generator::visit(ExecutionPlan ep) {
  ExecutionPlanVisitor::visit(ep);
  dump();
}

bool pending_packet_borrow_next_chunk(const ExecutionPlanNode *ep_node) {
  assert(ep_node);
  std::vector<ExecutionPlanNode_ptr> nodes;

  auto next = ep_node->get_next();
  nodes.insert(nodes.end(), next.begin(), next.end());

  while (nodes.size()) {
    auto node = nodes[0];
    nodes.erase(nodes.begin());

    auto module = node->get_module();
    assert(module);

    if (module->get_target() != Target::BMv2SimpleSwitchgRPC) {
      continue;
    }

    auto bdd_node = module->get_node();
    assert(bdd_node);

    if (bdd_node->get_type() == BDD::Node::NodeType::CALL) {
      auto call_node = static_cast<const BDD::Call *>(bdd_node.get());
      if (call_node->get_call().function_name == "packet_borrow_next_chunk") {
        return true;
      }
    }

    auto branches = node->get_next();
    nodes.insert(nodes.end(), branches.begin(), branches.end());
  }

  return false;
}

void BMv2SimpleSwitchgRPC_Generator::visit(const ExecutionPlanNode *ep_node) {
  auto mod = ep_node->get_module();
  auto next = ep_node->get_next();

  mod->visit(*this);

  auto pending_packet_borrow_ep = pending_packet_borrow_next_chunk(ep_node);

  if (parsing_headers && !pending_packet_borrow_ep) {
    parser.accept();
  }

  parsing_headers = pending_packet_borrow_ep;

  for (auto branch : next) {
    if (ep_node->get_module()->get_type() ==
            Module::ModuleType::BMv2SimpleSwitchgRPC_If &&
        pending_packet_borrow_ep &&
        !pending_packet_borrow_next_chunk(branch.get())) {
      parser.reject();
    }

    branch->visit(*this);
  }
}

void BMv2SimpleSwitchgRPC_Generator::visit(
    const targets::BMv2SimpleSwitchgRPC::Else *node) {
  local_vars.push();
  metadata.push();
  parser.push_on_false();

  pad(ingress.apply_block, ingress.lvl);
  ingress.apply_block << "else {\n";

  ingress.lvl++;
}

void BMv2SimpleSwitchgRPC_Generator::visit(
    const targets::BMv2SimpleSwitchgRPC::EthernetConsume *node) {
  header_field_t dstAddr(48, "dstAddr");
  header_field_t srcAddr(48, "srcAddr");
  header_field_t etherType(16, "etherType");

  std::vector<header_field_t> fields = {dstAddr, srcAddr, etherType};
  auto chunk = node->get_chunk();
  auto label = "ethernet";

  headers.emplace_back(chunk, label, fields);

  assert(parsing_headers);
  parser.add_extractor(label);

  deparser.headers_labels.push_back(label);
}

void BMv2SimpleSwitchgRPC_Generator::visit(
    const targets::BMv2SimpleSwitchgRPC::EthernetModify *node) {
  auto ethernet_chunk = node->get_ethernet_chunk();
  auto modifications = node->get_modifications();

  auto field = std::string();
  auto offset = 0u;

  for (auto mod : modifications) {
    auto byte = mod.byte;
    auto expr = mod.expr;

    auto modified_byte = BDD::solver_toolbox.exprBuilder->Extract(
        ethernet_chunk, byte * 8, klee::Expr::Int8);

    field_header_from_packet_chunk(modified_byte, field, offset);

    pad(ingress.apply_block, ingress.lvl);
    ingress.apply_block << field << " = ";
    ingress.apply_block << field << " & ";
    ingress.apply_block << "(";
    ingress.apply_block << "(";
    ingress.apply_block << "(";
    auto str = transpile(expr);
    str.erase(1,9); // remove bit<8>...
    ingress.apply_block << str;
    ingress.apply_block << ")";
    ingress.apply_block << " << ";
    ingress.apply_block << offset;
    ingress.apply_block << ")";
    ingress.apply_block << " | ";

    uint64_t mask = 0;
    for (auto bit = 0u; bit < offset; bit++) {
      mask = (mask << 1) | 1;
    }

    ingress.apply_block << mask;
    ingress.apply_block << ")";

    ingress.apply_block << ";\n";

    assert(field.size());
  }
}

void BMv2SimpleSwitchgRPC_Generator::visit(
    const targets::BMv2SimpleSwitchgRPC::Forward *node) {
  pad(ingress.apply_block, ingress.lvl);
  ingress.apply_block << "forward(" << node->get_port() << ");\n";

  auto closed = ingress.close_if_clauses(ingress.apply_block);

  for (auto i = 0; i < closed; i++) {
    parser.pop();
    local_vars.pop();
    metadata.pop();
  }
}

void BMv2SimpleSwitchgRPC_Generator::visit(
    const targets::BMv2SimpleSwitchgRPC::Drop *node) {
  pad(ingress.apply_block, ingress.lvl);
  ingress.apply_block << "drop();\n";

  auto closed = ingress.close_if_clauses(ingress.apply_block);

  for (auto i = 0; i < closed; i++) {
    parser.pop();
    local_vars.pop();
    metadata.pop();
  }
}

void BMv2SimpleSwitchgRPC_Generator::visit(
    const targets::BMv2SimpleSwitchgRPC::If *node) {
  parser.add_condition(transpile(node->get_condition(), true));

  local_vars.push();
  metadata.push();

  pad(ingress.apply_block, ingress.lvl);
  ingress.apply_block << "if (";
  ingress.apply_block << transpile(node->get_condition(), true);
  ingress.apply_block << ") {\n";

  ingress.lvl++;
  ingress.pending_ifs.push(true);
}

void BMv2SimpleSwitchgRPC_Generator::visit(
    const targets::BMv2SimpleSwitchgRPC::Ignore *node) {}

void BMv2SimpleSwitchgRPC_Generator::visit(
    const targets::BMv2SimpleSwitchgRPC::IPv4Consume *node) {
  header_field_t version_ihl(8, "version_ihl");
  header_field_t diff_serv(8, "diff_serv");
  header_field_t total_len(16, "total_len");
  header_field_t id(16, "id");
  header_field_t flags(3, "flags");
  header_field_t frag_offset(13, "frag_offset");
  header_field_t ttl(8, "ttl");
  header_field_t proto(8, "proto");
  header_field_t checksum(16, "checksum");
  header_field_t src_addr(32, "src_addr");
  header_field_t dst_addr(32, "dst_addr");

  std::vector<header_field_t> fields = {
      version_ihl, diff_serv, total_len, id,       flags,   frag_offset,
      ttl,         proto,     checksum,  src_addr, dst_addr};

  auto chunk = node->get_chunk();
  auto label = "ipv4";

  headers.emplace_back(chunk, label, fields);

  assert(parsing_headers);
  parser.add_extractor(label);

  deparser.headers_labels.push_back(label);
}

void BMv2SimpleSwitchgRPC_Generator::visit(
    const targets::BMv2SimpleSwitchgRPC::IPv4Modify *node) {
  assert(false && "TODO");
}

void BMv2SimpleSwitchgRPC_Generator::visit(
    const targets::BMv2SimpleSwitchgRPC::IPOptionsConsume *node) {
  auto chunk = node->get_chunk();
  auto length = node->get_length();

  header_field_t options(320, "options", length);

  std::vector<header_field_t> fields = {options};

  auto label = "ipv4_options";

  headers.emplace_back(chunk, label, fields);

  klee::ref<klee::Expr> length32 = length;

  // this is ok because we don't expect the length of an ipv4 packet
  // to not fit in 32 bits
  if (length->getWidth() < klee::Expr::Int32) {
    length32 = BDD::solver_toolbox.exprBuilder->ZExt(length, klee::Expr::Int32);
  } else if (length->getWidth() > klee::Expr::Int32) {
    length32 =
        BDD::solver_toolbox.exprBuilder->Extract(length, 0, klee::Expr::Int32);
  }

  assert(parsing_headers);
  parser.add_extractor(label, transpile(length32));

  deparser.headers_labels.push_back(label);
}

void BMv2SimpleSwitchgRPC_Generator::visit(
    const targets::BMv2SimpleSwitchgRPC::IPOptionsModify *node) {
  assert(false && "TODO");
}

void BMv2SimpleSwitchgRPC_Generator::visit(
    const targets::BMv2SimpleSwitchgRPC::TcpUdpConsume *node) {
  header_field_t src_port(16, "src_port");
  header_field_t dst_port(16, "dst_port");

  std::vector<header_field_t> fields = {src_port, dst_port};

  auto chunk = node->get_chunk();
  auto label = "tcp_udp";

  headers.emplace_back(chunk, label, fields);

  assert(parsing_headers);
  parser.add_extractor(label);

  deparser.headers_labels.push_back(label);
}

void BMv2SimpleSwitchgRPC_Generator::visit(
    const targets::BMv2SimpleSwitchgRPC::TcpUdpModify *node) {
  assert(false && "TODO");
}

void BMv2SimpleSwitchgRPC_Generator::visit(
    const targets::BMv2SimpleSwitchgRPC::SendToController *node) {
  auto code_path = node->get_metadata_code_path();

  pad(ingress.apply_block, ingress.lvl);
  ingress.apply_block << "send_to_controller(" << code_path << ");\n";

  auto closed = ingress.close_if_clauses(ingress.apply_block);

  for (auto i = 0; i < closed; i++) {
    parser.pop();
    local_vars.pop();
    metadata.pop();
  }
}

void BMv2SimpleSwitchgRPC_Generator::visit(
    const targets::BMv2SimpleSwitchgRPC::SetupExpirationNotifications *node) {
  // FIXME: assert(false && "TODO");
}

void BMv2SimpleSwitchgRPC_Generator::visit(
    const targets::BMv2SimpleSwitchgRPC::TableLookup *node) {
  auto keys = node->get_keys();
  auto params = node->get_params();
  auto bdd_function = node->get_bdd_function();
  auto has_this_key_labels = node->get_map_has_this_key_labels();
  auto table_id = node->get_table_id();

  assert(keys.size());

  std::stringstream code_table_id;
  code_table_id << bdd_function;
  code_table_id << "_";
  code_table_id << table_id;

  std::vector<std::string> params_type;
  for (auto param : params) {
    assert(param.exprs.size());
    auto param_type = p4_type_from_expr(param.exprs[0]);
    params_type.push_back(param_type);
  }

  std::vector<std::vector<std::string>> assignments;

  for (auto key : keys) {
    assignments.push_back(assign_key_bytes(key.expr));
  }

  assert(node->get_node());

  std::vector<std::string> key_bytes_label;
  for (unsigned i = 0; i < assignments[0].size(); i++) {
    assert(i < ingress.key_bytes.size());
    key_bytes_label.push_back(ingress.key_bytes[i].label);
  }

  std::vector<metadata_t> new_metadata;

  for (auto i = 0u; i < params.size(); i++) {
    std::stringstream meta_label;
    meta_label << code_table_id.str();
    meta_label << "_" << i;

    metadata_t meta_param(meta_label.str(), params[i].exprs);
    metadata.append(meta_param);
    new_metadata.push_back(meta_param);
  }

  metadata_t meta_tag_param(code_table_id.str() + "_tag", 32);
  metadata.append(meta_tag_param);
  new_metadata.push_back(meta_tag_param);

  table_t table(code_table_id.str(), key_bytes_label, params_type,
                new_metadata);
  ingress.tables.push_back(table);

  if (keys.size() == 1) {
    for (auto assignment : assignments[0]) {
      pad(ingress.apply_block, ingress.lvl);
      ingress.apply_block << assignment << ";\n";
    }
  } else {
    for (auto i = 0u; i < keys.size(); i++) {
      auto key = keys[i];
      auto key_assignments = assignments[i];

      pad(ingress.apply_block, ingress.lvl);
      ingress.apply_block << "if (";
      ingress.apply_block << transpile(key.condition);
      ingress.apply_block << ") {\n";

      ingress.lvl++;
      for (auto assignment : key_assignments) {
        pad(ingress.apply_block, ingress.lvl);
        ingress.apply_block << assignment << ";\n";
      }

      ingress.lvl--;
      pad(ingress.apply_block, ingress.lvl);
      ingress.apply_block << "}\n";
    }
  }

  if (has_this_key_labels.size()) {
    std::string hit_var_label;

    for (auto has_this_key_label : has_this_key_labels) {
      var_t hit_var(table.label + "_hit", has_this_key_label, 1);
      hit_var_label = hit_var.label;
      local_vars.append(hit_var);
    }

    pad(ingress.apply_block, ingress.lvl);
    ingress.apply_block << "bool " << hit_var_label;
    ingress.apply_block << " = ";
    ingress.apply_block << table.label << ".apply().hit;\n";
  } else {
    pad(ingress.apply_block, ingress.lvl);
    ingress.apply_block << table.label << ".apply();\n";
  }
}

void BMv2SimpleSwitchgRPC_Generator::visit(
    const targets::BMv2SimpleSwitchgRPC::Then *node) {
  parser.push_on_true();
}

}; // namespace synapse
