#include "ccold/time/zones.hpp"

#include <array>
#include <charconv>
#include <cstdio>
#include <stdexcept>
#include <system_error>

namespace ccold::time {
namespace {

using namespace std::chrono;

void check_year(int y) {
  if (y < kFirstSupportedYear || y > kLastSupportedYear) {
    throw std::out_of_range("US DST rule implemented for 2007-2099 only, got year " + std::to_string(y));
  }
}

int parse_int(std::string_view s, std::string_view whole) {
  int value = 0;
  const auto* first = s.data();
  const auto* end = s.data() + s.size();
  const auto [ptr, ec] = std::from_chars(first, end, value);
  if (ec != std::errc{} || ptr != end || s.empty()) {
    throw std::invalid_argument("malformed date/time: '" + std::string(whole) + "'");
  }
  return value;
}

void expect(bool ok, std::string_view whole) {
  if (!ok) {
    throw std::invalid_argument("malformed date/time: '" + std::string(whole) + "'");
  }
}

year_month_day ymd_of(sys_days d) { return year_month_day{d}; }

}  // namespace

std::chrono::hours standard_offset(UsZone zone) {
  return zone == UsZone::Eastern ? std::chrono::hours{-5} : std::chrono::hours{-6};
}

Instant dst_start(int y, UsZone zone) {
  check_year(y);
  const sys_days second_sunday_march{year{y} / March / Sunday[2]};
  // 02:00 local standard time.
  return Instant{second_sunday_march} + hours{2} - standard_offset(zone);
}

Instant dst_end(int y, UsZone zone) {
  check_year(y);
  const sys_days first_sunday_november{year{y} / November / Sunday[1]};
  // 02:00 local daylight time = 01:00 local standard time.
  return Instant{first_sunday_november} + hours{1} - standard_offset(zone);
}

bool is_dst(Instant t, UsZone zone) {
  // The calendar year in UTC is the right one: transitions are far from 1 January.
  const int y = static_cast<int>(ymd_of(floor<days>(t)).year());
  return t >= dst_start(y, zone) && t < dst_end(y, zone);
}

std::chrono::seconds utc_offset(Instant t, UsZone zone) {
  return standard_offset(zone) + (is_dst(t, zone) ? hours{1} : hours{0});
}

LocalTime to_local(Instant t, UsZone zone) {
  return LocalTime{(t + utc_offset(t, zone)).time_since_epoch()};
}

Instant to_utc(LocalTime local, UsZone zone) {
  const Instant as_if_utc{local.time_since_epoch()};
  const Instant if_standard = as_if_utc - standard_offset(zone);
  const Instant if_daylight = if_standard - hours{1};
  const bool standard_ok = !is_dst(if_standard, zone);
  const bool daylight_ok = is_dst(if_daylight, zone);
  if (standard_ok && daylight_ok) {
    throw std::invalid_argument("ambiguous local time " + format_local(local));
  }
  if (!standard_ok && !daylight_ok) {
    throw std::invalid_argument("non-existent local time " + format_local(local));
  }
  return standard_ok ? if_standard : if_daylight;
}

LocalTime local_at(Date d, int hour, int minute) {
  if (hour < 0 || hour > 23 || minute < 0 || minute > 59) {
    throw std::invalid_argument("invalid wall-clock time");
  }
  return LocalTime{d.time_since_epoch()} + hours{hour} + minutes{minute};
}

Date parse_date(std::string_view s) {
  expect(s.size() == 10 && s[4] == '-' && s[7] == '-', s);
  const year_month_day ymd{year{parse_int(s.substr(0, 4), s)},
                           month{static_cast<unsigned>(parse_int(s.substr(5, 2), s))},
                           day{static_cast<unsigned>(parse_int(s.substr(8, 2), s))}};
  expect(ymd.ok(), s);
  return sys_days{ymd};
}

Instant parse_utc(std::string_view s) {
  expect(s.size() == 20 && s[10] == 'T' && s[13] == ':' && s[16] == ':' && s[19] == 'Z', s);
  const int hh = parse_int(s.substr(11, 2), s);
  const int mm = parse_int(s.substr(14, 2), s);
  const int ss = parse_int(s.substr(17, 2), s);
  expect(hh <= 23 && mm <= 59 && ss <= 59, s);
  return Instant{parse_date(s.substr(0, 10))} + hours{hh} + minutes{mm} + seconds{ss};
}

std::string format_date(Date d) {
  const year_month_day ymd{d};
  std::array<char, 16> buf{};
  std::snprintf(buf.data(), buf.size(), "%04d-%02u-%02u", static_cast<int>(ymd.year()),
                static_cast<unsigned>(ymd.month()), static_cast<unsigned>(ymd.day()));
  return std::string(buf.data());
}

namespace {
std::string format_parts(sys_seconds t, const char* suffix) {
  const auto midnight = floor<days>(t);
  const hh_mm_ss<seconds> hms{t - midnight};
  std::array<char, 40> buf{};
  std::snprintf(buf.data(), buf.size(), "%sT%02d:%02d:%02d%s", format_date(midnight).c_str(),
                static_cast<int>(hms.hours().count()), static_cast<int>(hms.minutes().count()),
                static_cast<int>(hms.seconds().count()), suffix);
  return std::string(buf.data());
}
}  // namespace

std::string format_utc(Instant t) { return format_parts(t, "Z"); }

std::string format_local(LocalTime t) { return format_parts(sys_seconds{t.time_since_epoch()}, ""); }

}  // namespace ccold::time
