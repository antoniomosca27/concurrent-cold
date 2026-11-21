#pragma once
// The nine Census divisions of the NOAA CPC degree-day products, in NOAA's
// numbering (data/frozen/noaa_regions.csv), plus the CONUS aggregate.

#include <array>
#include <cstddef>
#include <string_view>

namespace ccold::data {

enum class Region : std::size_t {
  NewEngland,        // div1
  MiddleAtlantic,    // div2
  EastNorthCentral,  // div3
  WestNorthCentral,  // div4
  SouthAtlantic,     // div5
  EastSouthCentral,  // div6
  WestSouthCentral,  // div7
  Mountain,          // div8
  Pacific,           // div9
  Conus,
};

inline constexpr std::size_t kNumRegions = 10;
inline constexpr std::size_t kNumDivisions = 9;
// A bulletin covers its issue day and the next six days.
inline constexpr std::size_t kLeads = 7;

inline constexpr std::array<std::string_view, kNumRegions> kRegionCodes{"div1", "div2", "div3", "div4", "div5",
                                                                        "div6", "div7", "div8", "div9", "conus"};

[[nodiscard]] constexpr std::size_t index(Region r) noexcept { return static_cast<std::size_t>(r); }
[[nodiscard]] constexpr std::string_view code(Region r) noexcept { return kRegionCodes[index(r)]; }
[[nodiscard]] constexpr Region division(std::size_t i) noexcept { return static_cast<Region>(i); }

// Throws std::invalid_argument for an unknown code.
[[nodiscard]] Region region_from_code(std::string_view c);

}  // namespace ccold::data
