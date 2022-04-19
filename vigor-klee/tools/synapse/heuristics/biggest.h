#pragma once

#include "heuristic.h"
#include "score.h"

namespace synapse {

struct BiggestComparator : public HeuristicConfiguration {
  Score get_score(const ExecutionPlan &e) const override {
    Score score(e);
    score.add(Score::Category::NumberOfNodes, Score::Objective::MAXIMIZE);
    return score;
  }

  bool terminate_on_first_solution() const override { return false; }
};

using Biggest = Heuristic<BiggestComparator>;
} // namespace synapse
