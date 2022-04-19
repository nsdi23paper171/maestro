#pragma once

#include "../module.h"

namespace synapse {
namespace targets {
namespace netronome {

inline std::vector<Module_ptr> get_modules() {
  std::vector<Module_ptr> modules{
    // MODULE(MapGet),
  };

  return modules;
}

} // namespace netronome
} // namespace targets
} // namespace synapse
