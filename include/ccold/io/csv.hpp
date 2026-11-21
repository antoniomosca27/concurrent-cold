#pragma once
// Minimal RFC 4180 reader for the frozen inputs: comma separator, optional
// double-quoted fields with "" escapes, LF or CRLF record ends. Every record
// must have as many fields as the header; anything else is an error.

#include <cstddef>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace ccold::io {

class CsvTable {
 public:
  static CsvTable read(const std::filesystem::path& path);
  static CsvTable parse(std::string_view text, std::string source = "<memory>");

  [[nodiscard]] const std::vector<std::string>& header() const noexcept { return header_; }
  [[nodiscard]] std::size_t rows() const noexcept { return rows_.size(); }
  [[nodiscard]] const std::vector<std::string>& row(std::size_t r) const { return rows_.at(r); }
  [[nodiscard]] const std::string& at(std::size_t r, std::size_t c) const { return rows_.at(r).at(c); }
  [[nodiscard]] const std::string& source() const noexcept { return source_; }

  // Index of a named column; throws std::out_of_range naming the file if absent.
  [[nodiscard]] std::size_t column(std::string_view name) const;

 private:
  std::string source_;
  std::vector<std::string> header_;
  std::vector<std::vector<std::string>> rows_;
};

}  // namespace ccold::io
