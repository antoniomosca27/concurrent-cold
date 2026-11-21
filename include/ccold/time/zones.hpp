#pragma once
// Civil time in the two US time zones the data sources use.
//
// NOAA bulletins carry UTC stamps, EIA publishes at Eastern time and measures
// stocks at 9:00 Central time. We implement the US daylight-saving rule in
// force since 2007 (second Sunday of March 02:00 standard time to first Sunday
// of November 02:00 daylight time) instead of relying on the IANA database,
// whose availability in <chrono> differs across standard libraries.

#include <chrono>
#include <string>
#include <string_view>

namespace ccold::time {

using Instant = std::chrono::sys_seconds;      // a point in time, UTC
using LocalTime = std::chrono::local_seconds;  // wall-clock time, zone implied
using Date = std::chrono::sys_days;            // civil date

enum class UsZone { Eastern, Central };

// Years for which the 2007 rule is valid and implemented.
inline constexpr int kFirstSupportedYear = 2007;
inline constexpr int kLastSupportedYear = 2099;

[[nodiscard]] std::chrono::hours standard_offset(UsZone zone);  // -5h or -6h

// UTC instants at which daylight saving time starts and ends in year `y`.
[[nodiscard]] Instant dst_start(int y, UsZone zone);
[[nodiscard]] Instant dst_end(int y, UsZone zone);

[[nodiscard]] bool is_dst(Instant t, UsZone zone);
[[nodiscard]] std::chrono::seconds utc_offset(Instant t, UsZone zone);
[[nodiscard]] LocalTime to_local(Instant t, UsZone zone);

// Converts a wall-clock time to UTC. Throws std::invalid_argument for wall
// times that do not exist (spring gap) or are ambiguous (autumn overlap):
// the data never needs them, so a silent choice would only hide a bug.
[[nodiscard]] Instant to_utc(LocalTime local, UsZone zone);

[[nodiscard]] LocalTime local_at(Date d, int hour, int minute);

// ISO 8601 helpers: dates "2021-02-12", UTC instants "2021-02-12T23:00:00Z";
// local times are formatted without zone suffix, "2021-02-12T18:00:00".
[[nodiscard]] Date parse_date(std::string_view iso);
[[nodiscard]] Instant parse_utc(std::string_view iso);
[[nodiscard]] std::string format_date(Date d);
[[nodiscard]] std::string format_utc(Instant t);
[[nodiscard]] std::string format_local(LocalTime t);

}  // namespace ccold::time
