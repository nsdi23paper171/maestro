#pragma once

#include <fstream>
#include <stdio.h>
#include <streambuf>
#include <string>

#include "visitor.h"

#define GET_BOILERPLATE_PATH(fname)                                            \
  (std::string(__FILE__).substr(0, std::string(__FILE__).rfind("/")) + "/" +   \
   (fname))

namespace synapse {

class TargetCodeGenerator : public ExecutionPlanVisitor {
protected:
  struct code_builder_t {
    std::string code;

    code_builder_t(const std::string &boilerplate_fpath) {
      auto boilerplate_stream = std::ifstream(boilerplate_fpath);

      boilerplate_stream.seekg(0, std::ios::end);
      code.reserve(boilerplate_stream.tellg());
      boilerplate_stream.seekg(0, std::ios::beg);

      code.assign((std::istreambuf_iterator<char>(boilerplate_stream)),
                  std::istreambuf_iterator<char>());
    }

    void fill_mark(std::string marker_label, const std::string &content) {
      auto marker = "{{" + marker_label + "}}";

      auto delim = code.find(marker);
      assert(delim != std::string::npos);

      auto trimmed_content = content;

      while (trimmed_content.size() &&
             (trimmed_content[0] == ' ' || trimmed_content[0] == '\n')) {
        trimmed_content = trimmed_content.substr(1, trimmed_content.size() - 1);
      }

      while (trimmed_content.size() &&
             (trimmed_content[trimmed_content.size() - 1] == ' ' ||
              trimmed_content[trimmed_content.size() - 1] == '\n')) {
        trimmed_content = trimmed_content.substr(0, trimmed_content.size() - 1);
      }

      code.replace(delim, marker.size(), trimmed_content);
    }

    int get_indentation_level(std::string marker_label) {
      auto marker = "{{" + marker_label + "}}";

      auto delim = code.find(marker);
      assert(delim != std::string::npos);

      int spaces = 0;
      while (delim > 0 && code[delim - 1] == ' ') {
        spaces++;
        delim--;
      }

      return spaces / 2;
    }
  };

private:
  std::unique_ptr<std::ostream> os;
  std::string fpath;

protected:
  code_builder_t code_builder;
  const ExecutionPlan *original_ep;

public:
  TargetCodeGenerator(const std::string &boilerplate_fpath)
      : code_builder(boilerplate_fpath), original_ep(nullptr) {
    os = std::unique_ptr<std::ostream>(new std::ostream(std::cerr.rdbuf()));
  }

  void output_to_file(const std::string &_fpath) {
    fpath = _fpath;
    os = std::unique_ptr<std::ostream>(new std::ofstream(fpath));
  }

  void generate(ExecutionPlan &target_ep, const ExecutionPlan &_original_ep) {
    original_ep = &_original_ep;

    if (target_ep.get_nodes() == 0) {
      if (!fpath.size()) {
        return;
      }

      remove(fpath.c_str());
      return;
    }

    visit(target_ep);
    *os << code_builder.code;
  }
};

} // namespace synapse
