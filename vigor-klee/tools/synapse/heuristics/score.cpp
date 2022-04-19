#include "score.h"
#include "../modules/modules.h"

namespace synapse {

int Score::get_nr_nodes() const { return execution_plan.get_nodes(); }

int Score::get_nr_merged_tables() const {
  if (!execution_plan.get_root()) {
    return 0;
  }

  auto num_merged_tables = 0;
  auto nodes = std::vector<ExecutionPlanNode_ptr>{execution_plan.get_root()};

  while (nodes.size()) {
    auto node = nodes[0];
    nodes.erase(nodes.begin());

    auto module = node->get_module();
    if (module->get_type() ==
        Module::ModuleType::BMv2SimpleSwitchgRPC_TableLookup) {
      auto tableLookup =
          static_cast<targets::BMv2SimpleSwitchgRPC::TableLookup *>(
              module.get());

      auto merged = tableLookup->get_keys().size();

      if (merged > 1) {
        num_merged_tables += merged;
      }
    }

    for (auto branch : node->get_next()) {
      nodes.push_back(branch);
    }
  }

  return num_merged_tables;
}

int Score::get_depth() const { return execution_plan.get_depth(); }

int Score::get_nr_switch_nodes() const {
  auto switch_nodes = 0;

  const auto &nodes_per_target = execution_plan.get_nodes_per_target();
  auto switch_nodes_it = nodes_per_target.find(Target::BMv2SimpleSwitchgRPC);

  if (switch_nodes_it != nodes_per_target.end()) {
    switch_nodes = switch_nodes_it->second;
  }

  return switch_nodes;
}

int Score::get_nr_controller_nodes() const {
  auto controller_nodes = 0;

  const auto &nodes_per_target = execution_plan.get_nodes_per_target();
  auto controller_nodes_it = nodes_per_target.find(Target::x86);

  if (controller_nodes_it != nodes_per_target.end()) {
    controller_nodes = controller_nodes_it->second;
  }

  return controller_nodes;
}

int Score::get_nr_reordered_nodes() const {
  return execution_plan.get_reordered_nodes();
}

} // namespace synapse
