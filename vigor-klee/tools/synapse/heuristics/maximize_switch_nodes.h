#pragma once

#include "../modules/modules.h"
#include "heuristic.h"
#include "score.h"

namespace synapse {

struct MaximizeSwitchNodesComparator : public HeuristicConfiguration {
  Score get_score(const ExecutionPlan &e) const override {
    Score score(e);

    score.add(Score::Category::NumberOfMergedTables);
    score.add(Score::Category::NumberOfSwitchNodes);
    score.add(Score::Category::NumberOfNodes, Score::MINIMIZE);

    return score;
  }

  bool terminate_on_first_solution() const override { return true; }
};

using MaximizeSwitchNodes = Heuristic<MaximizeSwitchNodesComparator>;
} // namespace synapse
