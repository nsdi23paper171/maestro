#pragma once

#include "solver_toolbox.h"

#include <assert.h>

namespace BDD {

class BDD;
class Branch;
class Call;
class ReturnInit;
class ReturnProcess;
class ReturnRaw;
class Node;

class BDDVisitor {
public:
  enum Action {
    VISIT_CHILDREN,
    STOP
  };

public:
  void visit(const Branch *node);
  void visit(const Call *node);
  void visit(const ReturnInit *node);
  void visit(const ReturnProcess *node);
  void visit(const ReturnRaw *node);
  void visit(const BDD &bdd);

protected:
  virtual Action visitBranch(const Branch *node) = 0;
  virtual Action visitCall(const Call *node) = 0;
  virtual Action visitReturnInit(const ReturnInit *node) = 0;
  virtual Action visitReturnProcess(const ReturnProcess *node) = 0;

  virtual void visitInitRoot(const Node *root);
  virtual void visitProcessRoot(const Node *root);

  virtual Action visitReturnRaw(const ReturnRaw *node) {
    assert(false && "Something went wrong...");
  }
};
} // namespace BDD
