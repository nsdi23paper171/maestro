#pragma once

#include <algorithm>
#include <assert.h>
#include <vector>

#include "heuristics/heuristic.h"
#include "heuristics/score.h"

namespace synapse {

struct search_space_node_t {
  std::vector<std::shared_ptr<search_space_node_t>> space;
  std::shared_ptr<search_space_node_t> prev;
  int execution_plan_id;
  Module_ptr m;

  Score score;

  search_space_node_t(int _execution_plan_id, Score _score)
      : execution_plan_id(_execution_plan_id), score(_score) {}

  search_space_node_t(int _execution_plan_id, const Module_ptr &_m,
                      Score _score)
      : search_space_node_t(_execution_plan_id, _score) {
    m = _m;
  }
};

class SearchSpace {
private:
  struct pending_leaves_t {
    int execution_plan_id;
    std::vector<ExecutionPlan> eps;
    std::vector<Module_ptr> modules;

    pending_leaves_t() : execution_plan_id(-1) {}

    void reset() {
      execution_plan_id = -1;
      eps.clear();
      modules.clear();
    }

    void add(const ExecutionPlan &ep, const Module_ptr &module) {
      eps.push_back(ep);
      modules.push_back(module);
    }
  };

private:
  std::shared_ptr<search_space_node_t> root;
  std::vector<std::shared_ptr<search_space_node_t>> leaves;
  pending_leaves_t pending_leaves;

  const HeuristicConfiguration *hc;

public:
  SearchSpace(const HeuristicConfiguration *_hc, const ExecutionPlan &ep)
      : root(new search_space_node_t(ep.get_id(), _hc->get_score(ep))),
        hc(_hc) {
    leaves.push_back(root);
  }

  void add_leaves(const ExecutionPlan &ep, Module_ptr module,
                  const std::vector<ExecutionPlan> &next_eps) {
    std::vector<search_space_node_t> nodes;

    int execution_plan_id = ep.get_id();

    assert(pending_leaves.execution_plan_id == -1 ||
           pending_leaves.execution_plan_id == execution_plan_id);

    pending_leaves.execution_plan_id = execution_plan_id;

    for (auto ep : next_eps) {
      pending_leaves.add(ep, module);
    }
  }

  void submit_leaves() {
    auto search_space_node_matcher = [&](
        std::shared_ptr<search_space_node_t> node) {
      return node->execution_plan_id == pending_leaves.execution_plan_id;
    };

    auto found_it =
        std::find_if(leaves.begin(), leaves.end(), search_space_node_matcher);
    assert(found_it != leaves.end() && "Leaf not found");
    auto leaf = *found_it;
    leaves.erase(found_it);

    for (unsigned i = 0; i < pending_leaves.eps.size(); i++) {
      auto &ep = pending_leaves.eps[i];
      auto &module = pending_leaves.modules[i];

      leaf->space.emplace_back(
          new search_space_node_t(ep.get_id(), module, hc->get_score(ep)));
      leaf->space.back()->prev = leaf;
      leaves.push_back(leaf->space.back());
    }

    pending_leaves.reset();
  }

  const std::vector<std::shared_ptr<search_space_node_t>> &get_leaves() const {
    return leaves;
  }

  const std::shared_ptr<search_space_node_t> &get_root() const { return root; }
};
} // namespace synapse
