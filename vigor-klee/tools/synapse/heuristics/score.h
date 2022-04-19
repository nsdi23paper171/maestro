#pragma once

#include "../execution_plan/execution_plan.h"

#include <iostream>
#include <map>
#include <vector>

namespace synapse {

class Score {
public:
  enum Category {
    NumberOfReorderedNodes,
    NumberOfSwitchNodes,
    NumberOfNodes,
    NumberOfControllerNodes,
    NumberOfMergedTables,
    Depth,
  };

  enum Objective {
    MINIMIZE,
    MAXIMIZE
  };

private:
  typedef int (Score::*ComputerPtr)() const;

  const ExecutionPlan &execution_plan;
  std::map<Category, ComputerPtr> computers;

  // The order of the elements in this vector matters.
  // It defines a lexicographic order.
  std::vector<std::pair<Category, Objective>> categories;

  int get_nr_nodes() const;
  int get_nr_merged_tables() const;
  int get_depth() const;
  int get_nr_switch_nodes() const;
  int get_nr_controller_nodes() const;
  int get_nr_reordered_nodes() const;

public:
  Score(const ExecutionPlan &_execution_plan)
      : execution_plan(_execution_plan) {
    computers = { { NumberOfReorderedNodes, &Score::get_nr_reordered_nodes },
                  { NumberOfNodes, &Score::get_nr_nodes },
                  { NumberOfMergedTables, &Score::get_nr_merged_tables },
                  { NumberOfSwitchNodes, &Score::get_nr_switch_nodes },
                  { NumberOfControllerNodes, &Score::get_nr_controller_nodes },
                  { Depth, &Score::get_depth }, };
  }
  Score(const Score &score)
      : execution_plan(score.execution_plan), computers(score.computers),
        categories(score.categories) {}

  void add(Category category, Objective objective = Objective::MAXIMIZE) {
    auto found_it =
        std::find_if(categories.begin(), categories.end(),
                     [&](const std::pair<Category, Objective> &saved) {
          return saved.first == category;
        });

    assert(found_it == categories.end() && "Category already inserted");

    categories.emplace_back(category, objective);
  }

  int get(Category category) const {
    auto found_it = computers.find(category);

    if (found_it == computers.end()) {
      Log::err() << "\nScore error: " << category
                 << " not found in lookup table.\n";
      exit(1);
    }

    auto computer = found_it->second;
    return (this->*computer)();
  }

  inline bool operator<(const Score &other) {
    for (auto category_objective : categories) {
      auto category = category_objective.first;
      auto objective = category_objective.second;

      auto this_score = get(category);
      auto other_score = other.get(category);

      if (objective == Objective::MINIMIZE) {
        this_score *= -1;
        other_score *= -1;
      }

      if (this_score > other_score) {
        return false;
      }

      if (this_score < other_score) {
        return true;
      }
    }

    return false;
  }

  inline bool operator==(const Score &other) {
    for (auto category_objective : categories) {
      auto category = category_objective.first;
      auto objective = category_objective.second;

      auto this_score = get(category);
      auto other_score = other.get(category);

      if (objective == Objective::MINIMIZE) {
        this_score *= -1;
        other_score *= -1;
      }

      if (this_score != other_score) {
        return false;
      }
    }

    return true;
  }

  inline bool operator>(const Score &other) {
    return !((*this) < other) && !((*this) == other);
  }

  inline bool operator<=(const Score &other) { return !((*this) > other); }
  inline bool operator>=(const Score &other) { return !((*this) < other); }
  inline bool operator!=(const Score &other) { return !((*this) == other); }

  friend std::ostream &operator<<(std::ostream &os, const Score &dt);
};

inline std::ostream &operator<<(std::ostream &os, const Score &score) {
  os << "<";

  bool first = true;
  for (auto category_objective : score.categories) {
    auto category = category_objective.first;
    auto objective = category_objective.second;

    auto value = score.get(category);

    if (objective == Score::Objective::MINIMIZE) {
      value *= -1;
    }

    if (!first) {
      os << ",";
    }

    os << value;

    first &= false;
  }

  os << ">";
  return os;
}
} // namespace synapse