#include "ini_config.h"

#include "config/atomic_file.h"
#include "util/string_utils.h"

#include <fstream>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

namespace {

  void setError(std::string* error, std::string message) {
    if (error != nullptr) {
      *error = std::move(message);
    }
  }

  [[nodiscard]] bool isSectionHeader(std::string_view trimmedLine) {
    return trimmedLine.size() >= 2 && trimmedLine.front() == '[' && trimmedLine.back() == ']';
  }

  [[nodiscard]] bool isCommentOrBlank(std::string_view trimmedLine) {
    return trimmedLine.empty() || trimmedLine.front() == '#' || trimmedLine.front() == ';';
  }

  // The leading token of a flat config line: everything before the first '=', or
  // whitespace (xsettingsd writes `Key "value"`).
  [[nodiscard]] std::string flatKeyName(std::string_view trimmedLine) {
    const std::size_t end = trimmedLine.find_first_of("= \t");
    return std::string(end == std::string_view::npos ? trimmedLine : trimmedLine.substr(0, end));
  }

  [[nodiscard]] std::optional<std::vector<std::string>>
  readLines(const std::filesystem::path& path, std::string* error) {
    std::ifstream in(path);
    if (!in) {
      setError(error, "failed to read " + path.string());
      return std::nullopt;
    }
    std::vector<std::string> lines;
    for (std::string line; std::getline(in, line);) {
      if (!line.empty() && line.back() == '\r') {
        line.pop_back();
      }
      lines.push_back(std::move(line));
    }
    if (!in.eof()) {
      setError(error, "failed to read " + path.string());
      return std::nullopt;
    }
    return lines;
  }

  [[nodiscard]] bool writeLines(
      const std::filesystem::path& path, const std::vector<std::string>& lines, std::string* error
  ) {
    std::string content;
    content.reserve(lines.size() * 32);
    for (const std::string& line : lines) {
      content += line;
      content += '\n';
    }
    if (!writeTextFileAtomic(path, content)) {
      setError(error, "failed to write " + path.string());
      return false;
    }
    return true;
  }

} // namespace

namespace kinetic_settings {

  bool setKey(
      const std::filesystem::path& path,
      std::string_view section,
      std::string_view key,
      std::string_view value,
      bool createIfMissing,
      std::string* error
  ) {
    std::error_code ec;
    const std::filesystem::file_status status = std::filesystem::status(path, ec);
    if (ec) {
      setError(error, "failed to inspect " + path.string() + ": " + ec.message());
      return false;
    }
    const bool exists = std::filesystem::is_regular_file(status);
    if (!exists && !createIfMissing) {
      return true;
    }

    std::vector<std::string> lines;
    if (exists) {
      auto read = readLines(path, error);
      if (!read) {
        return false;
      }
      lines = std::move(*read);
    }

    // Locate the target group. sectionHeader is its [header] line (npos when the
    // group is absent); sectionEnd is the first line after its body.
    std::size_t sectionHeader = std::string::npos;
    std::size_t sectionEnd = std::string::npos;
    for (std::size_t i = 0; i < lines.size(); ++i) {
      const std::string trimmed = StringUtils::trim(lines[i]);
      if (!isSectionHeader(trimmed)) {
        continue;
      }
      if (sectionHeader != std::string::npos) {
        sectionEnd = i;
        break;
      }
      if (trimmed.substr(1, trimmed.size() - 2) == section) {
        sectionHeader = i;
      }
    }
    if (sectionHeader != std::string::npos && sectionEnd == std::string::npos) {
      sectionEnd = lines.size();
    }

    bool changed = false;
    const std::string wanted = std::string(key) + '=' + std::string(value);
    if (sectionHeader == std::string::npos) {
      if (value.empty() || !createIfMissing) {
        return true;
      }
      if (!lines.empty() && !StringUtils::trim(lines.back()).empty()) {
        lines.emplace_back();
      }
      lines.emplace_back("[" + std::string(section) + "]");
      lines.push_back(wanted);
      changed = true;
    } else {
      std::size_t keyLine = std::string::npos;
      for (std::size_t i = sectionHeader + 1; i < sectionEnd; ++i) {
        const std::string trimmed = StringUtils::trim(lines[i]);
        if (isCommentOrBlank(trimmed)) {
          continue;
        }
        const std::size_t equals = trimmed.find('=');
        if (equals != std::string_view::npos && StringUtils::trim(trimmed.substr(0, equals)) == key) {
          keyLine = i;
          break;
        }
      }

      if (keyLine != std::string::npos) {
        if (value.empty()) {
          lines.erase(lines.begin() + static_cast<std::ptrdiff_t>(keyLine));
          changed = true;
        } else if (lines[keyLine] != wanted) {
          lines[keyLine] = wanted;
          changed = true;
        }
      } else if (!value.empty()) {
        // Append at the end of the group, keeping its trailing blank lines.
        std::size_t insertAt = sectionEnd;
        while (insertAt > sectionHeader + 1 && StringUtils::trim(lines[insertAt - 1]).empty()) {
          --insertAt;
        }
        lines.insert(lines.begin() + static_cast<std::ptrdiff_t>(insertAt), wanted);
        changed = true;
      }
    }

    if (!changed) {
      return true;
    }
    return writeLines(path, lines, error);
  }

  bool setFlatKey(
      const std::filesystem::path& path,
      std::string_view key,
      std::string_view value,
      std::string_view separator,
      bool quote,
      bool createIfMissing,
      std::string* error
  ) {
    std::error_code ec;
    const std::filesystem::file_status status = std::filesystem::status(path, ec);
    if (ec) {
      setError(error, "failed to inspect " + path.string() + ": " + ec.message());
      return false;
    }
    const bool exists = std::filesystem::is_regular_file(status);
    if (!exists && !createIfMissing) {
      return true;
    }

    std::vector<std::string> lines;
    if (exists) {
      auto read = readLines(path, error);
      if (!read) {
        return false;
      }
      lines = std::move(*read);
    }

    std::string wanted = std::string(key) + std::string(separator) + std::string(value);
    if (quote && !value.empty()) {
      wanted = std::string(key) + std::string(separator) + '"' + std::string(value) + '"';
    }

    bool found = false;
    bool changed = false;
    for (auto it = lines.begin(); it != lines.end(); ++it) {
      const std::string trimmed = StringUtils::trim(*it);
      if (isCommentOrBlank(trimmed) || flatKeyName(trimmed) != key) {
        continue;
      }
      found = true;
      if (value.empty()) {
        lines.erase(it);
        changed = true;
      } else if (*it != wanted) {
        *it = wanted;
        changed = true;
      }
      break;
    }
    if (!found && !value.empty()) {
      lines.push_back(wanted);
      changed = true;
    }

    if (!changed) {
      return true;
    }
    return writeLines(path, lines, error);
  }

} // namespace kinetic_settings
