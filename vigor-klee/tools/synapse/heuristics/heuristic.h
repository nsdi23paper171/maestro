#pragma once

#include "../execution_plan/execution_plan.h"
#include "score.h"

#include <set>

namespace synapse {

struct HeuristicConfiguration {
  virtual Score get_score(const ExecutionPlan &e) const = 0;

  virtual bool operator()(const ExecutionPlan &e1,
                          const ExecutionPlan &e2) const {
    return get_score(e1) > get_score(e2);
  }
  virtual bool terminate_on_first_solution() const = 0;
};

template <class T> class Heuristic {
  static_assert(std::is_base_of<HeuristicConfiguration, T>::value,
                "T must inherit from HeuristicConfiguration");

protected:
  std::multiset<ExecutionPlan, T> execution_plans;
  T configuration;

private:
  typename std::set<ExecutionPlan, T>::iterator get_best_it() const {
    assert(execution_plans.size());
    return execution_plans.begin();
  }

  typename std::set<ExecutionPlan, T>::iterator get_next_it() const {
    assert(execution_plans.size());

    auto conf = static_cast<const HeuristicConfiguration *>(&configuration);
    auto it = execution_plans.begin();

    while (!conf->terminate_on_first_solution() && !it->get_next_node() &&
           it != execution_plans.end()) {
      ++it;
    }

    if (!it->get_next_node()) {
      it = execution_plans.end();
    }

    return it;
  }

public:
  bool finished() const { return get_next_it() == execution_plans.end(); }

  ExecutionPlan get() { return *get_best_it(); }

  std::vector<ExecutionPlan> get_all() const {
    std::vector<ExecutionPlan> eps;
    eps.assign(execution_plans.begin(), execution_plans.end());
    return eps;
  }

  ExecutionPlan pop() {
    auto it = get_next_it();
    auto copy = *it;

    execution_plans.erase(it);
    return copy;
  }

  void add(const std::vector<ExecutionPlan> &next_eps) {
    assert(next_eps.size());

    for (auto ep : next_eps) {
      auto found = false;

      for (auto saved_ep : execution_plans) {
        if (saved_ep == ep) {
          found = true;
          break;
        }
      }

      if (found) {
        continue;
      }

      execution_plans.insert(ep);
    }
  }

  int size() const { return execution_plans.size(); }

  const T *get_cfg() const { return &configuration; }

  Score get_score(const ExecutionPlan &e) const {
    auto conf = static_cast<const HeuristicConfiguration *>(&configuration);
    return conf->get_score(e);
  }
};
} // namespace synapse
