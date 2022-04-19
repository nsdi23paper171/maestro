#pragma once

#include "heuristic.h"
#include "score.h"

namespace synapse {

struct LeastReorderedComparator : public HeuristicConfiguration {
  Score get_score(const ExecutionPlan &e) const override {
    Score score(e);

    score.add(Score::Category::NumberOfReorderedNodes,
              Score::Objective::MINIMIZE);
    score.add(Score::Category::NumberOfNodes);

    return score;
  }

  bool terminate_on_first_solution() const override { return false; }
};

using LeastReordered = Heuristic<LeastReorderedComparator>;
} // namespace synapse
