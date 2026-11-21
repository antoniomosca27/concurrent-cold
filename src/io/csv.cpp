#include "ccold/io/csv.hpp"

#include <charconv>
#include <fstream>
#include <sstream>
#include <stdexcept>

namespace ccold::io {
namespace {

[[noreturn]] void fail(const std::string& source, std::size_t line, const std::string& what) {
  throw std::runtime_error(source + ":" + std::to_string(line) + ": " + what);
}

}  // namespace

CsvTable CsvTable::read(const std::filesystem::path& path) {
  std::ifstream in(path, std::ios::binary);
  if (!in) {
    throw std::runtime_error("cannot open " + path.string());
  }
  std::ostringstream buffer;
  buffer << in.rdbuf();
  return parse(buffer.str(), path.string());
}

CsvTable CsvTable::parse(std::string_view text, std::string source) {
  CsvTable table;
  table.source_ = std::move(source);
  std::vector<std::vector<std::string>> records;
  std::vector<std::string> record;
  std::string field;
  bool in_quotes = false;
  bool field_was_quoted = false;
  std::size_t line = 1;
  std::size_t record_line = 1;

  auto end_field = [&] {
    record.push_back(std::move(field));
    field.clear();
    field_was_quoted = false;
  };
  auto end_record = [&] {
    end_field();
    records.push_back(std::move(record));
    record.clear();
  };

  for (std::size_t i = 0; i < text.size(); ++i) {
    const char c = text[i];
    if (in_quotes) {
      if (c == '"') {
        if (i + 1 < text.size() && text[i + 1] == '"') {
          field.push_back('"');
          ++i;
        } else {
          in_quotes = false;
        }
      } else {
        if (c == '\n') {
          ++line;
        }
        field.push_back(c);
      }
      continue;
    }
    switch (c) {
      case '"':
        if (!field.empty() || field_was_quoted) {
          fail(table.source_, line, "quote inside an unquoted field");
        }
        in_quotes = true;
        field_was_quoted = true;
        break;
      case ',':
        end_field();
        break;
      case '\r':
        if (i + 1 >= text.size() || text[i + 1] != '\n') {
          fail(table.source_, line, "bare carriage return");
        }
        break;
      case '\n':
        end_record();
        ++line;
        record_line = line;
        break;
      default:
        if (field_was_quoted) {
          fail(table.source_, line, "characters after a closing quote");
        }
        field.push_back(c);
    }
  }
  if (in_quotes) {
    fail(table.source_, record_line, "unterminated quoted field");
  }
  if (!field.empty() || !record.empty() || field_was_quoted) {
    end_record();  // last record without a trailing newline
  }
  if (records.empty()) {
    fail(table.source_, 1, "empty file");
  }

  table.header_ = std::move(records.front());
  const std::size_t width = table.header_.size();
  table.rows_.reserve(records.size() - 1);
  for (std::size_t r = 1; r < records.size(); ++r) {
    if (records[r].size() != width) {
      fail(table.source_, r + 1,
           "record has " + std::to_string(records[r].size()) + " fields, header has " + std::to_string(width));
    }
    table.rows_.push_back(std::move(records[r]));
  }
  return table;
}

int to_int(std::string_view field, std::string_view context) {
  int value = 0;
  const char* first = field.data();
  const char* end = field.data() + field.size();
  const auto [ptr, ec] = std::from_chars(first, end, value);
  if (field.empty() || ec != std::errc{} || ptr != end) {
    throw std::runtime_error(std::string(context) + ": not an integer: '" + std::string(field) + "'");
  }
  return value;
}

std::optional<int> to_optional_int(std::string_view field, std::string_view context) {
  if (field.empty()) {
    return std::nullopt;
  }
  return to_int(field, context);
}

std::size_t CsvTable::column(std::string_view name) const {
  for (std::size_t c = 0; c < header_.size(); ++c) {
    if (header_[c] == name) {
      return c;
    }
  }
  throw std::out_of_range(source_ + ": no column '" + std::string(name) + "'");
}

}  // namespace ccold::io
