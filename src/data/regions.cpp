#include "ccold/data/regions.hpp"

#include <stdexcept>
#include <string>

namespace ccold::data {

Region region_from_code(std::string_view c) {
  for (std::size_t i = 0; i < kNumRegions; ++i) {
    if (kRegionCodes[i] == c) {
      return static_cast<Region>(i);
    }
  }
  throw std::invalid_argument("unknown region code '" + std::string(c) + "'");
}

}  // namespace ccold::data
