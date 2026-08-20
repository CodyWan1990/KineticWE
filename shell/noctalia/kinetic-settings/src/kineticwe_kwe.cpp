#include "kineticwe_kwe.h"

#include <charconv>
#include <cstdlib>
#include <fstream>
#include <sstream>

namespace {

  // The compositor's settings root is always ~/.config/kineticwe, regardless
  // of the process's XDG_CONFIG_HOME: the session script only redirects
  // XDG_CONFIG_HOME for the compositor process itself, so the shell (which
  // embeds kinetic-settings) must resolve the root explicitly. KWE_CONFIG_HOME
  // is honored as an override for testing/dev.
  std::filesystem::path configHome() {
    if (const char* kwe = std::getenv("KWE_CONFIG_HOME"); kwe != nullptr && *kwe != '\0') {
      return kwe;
    }
    if (const char* home = std::getenv("HOME"); home != nullptr && *home != '\0') {
      return std::filesystem::path(home) / ".config" / "kineticwe";
    }
    return ".";
  }

  // KConfig INI list encoding: items separated by ';', '\' and ';' escaped.
  std::string escapeListItem(std::string_view item) {
    std::string out;
    out.reserve(item.size());
    for (const char c : item) {
      if (c == '\\' || c == ';') {
        out.push_back('\\');
      }
      out.push_back(c);
    }
    return out;
  }

  std::string unescapeListItem(std::string_view item) {
    std::string out;
    out.reserve(item.size());
    for (std::size_t i = 0; i < item.size(); ++i) {
      if (item[i] == '\\' && i + 1 < item.size()) {
        ++i;
      }
      out.push_back(item[i]);
    }
    return out;
  }

} // namespace

std::filesystem::path KineticweKwe::defaultPath() {
  return configHome() / "kineticwe.kwe";
}

bool KineticweKwe::load() {
  m_groups.clear();
  std::ifstream in(path());
  if (!in.is_open()) {
    return false;
  }
  std::string currentGroup;
  std::string line;
  while (std::getline(in, line)) {
    while (!line.empty() && (line.back() == '\r' || line.back() == '\n')) {
      line.pop_back();
    }
    if (line.empty() || line.front() == '#' || line.front() == ';') {
      continue;
    }
    if (line.front() == '[' && line.back() == ']') {
      currentGroup = line.substr(1, line.size() - 2);
      m_groups.try_emplace(currentGroup, emptyGroup());
      continue;
    }
    const std::size_t eq = line.find('=');
    if (eq == std::string::npos) {
      continue;
    }
    std::string key = line.substr(0, eq);
    while (!key.empty() && key.back() == ' ') {
      key.pop_back();
    }
    std::string value = line.substr(eq + 1);
    if (!value.empty() && value.front() == ' ') {
      value.erase(value.begin());
    }
    m_groups[currentGroup][key] = value;
  }
  return true;
}

std::optional<std::string> KineticweKwe::readString(std::string_view group, std::string_view key) const {
  const auto groupIt = m_groups.find(std::string(group));
  if (groupIt == m_groups.end()) {
    return std::nullopt;
  }
  const auto keyIt = groupIt->second.find(std::string(key));
  if (keyIt == groupIt->second.end()) {
    return std::nullopt;
  }
  return keyIt->second;
}

bool KineticweKwe::readBool(std::string_view group, std::string_view key, bool fallback) const {
  const auto value = readString(group, key);
  if (!value.has_value()) {
    return fallback;
  }
  if (value == "true") {
    return true;
  }
  if (value == "false") {
    return false;
  }
  return fallback;
}

int KineticweKwe::readInt(std::string_view group, std::string_view key, int fallback) const {
  const auto value = readString(group, key);
  if (!value.has_value()) {
    return fallback;
  }
  int parsed = 0;
  const auto [end, error] = std::from_chars(value->data(), value->data() + value->size(), parsed);
  if (error != std::errc() || end != value->data() + value->size()) {
    return fallback;
  }
  return parsed;
}

double KineticweKwe::readDouble(std::string_view group, std::string_view key, double fallback) const {
  const auto value = readString(group, key);
  if (!value.has_value()) {
    return fallback;
  }
  try {
    return std::stod(*value);
  } catch (...) {
    return fallback;
  }
}

std::vector<std::string> KineticweKwe::readStringList(std::string_view group, std::string_view key) const {
  std::vector<std::string> out;
  const auto value = readString(group, key);
  if (!value.has_value() || value->empty()) {
    return out;
  }
  std::size_t start = 0;
  for (std::size_t i = 0; i <= value->size(); ++i) {
    const char c = i < value->size() ? (*value)[i] : ';';
    if (c == ';') {
      out.push_back(unescapeListItem(std::string_view(*value).substr(start, i - start)));
      start = i + 1;
    }
  }
  return out;
}

void KineticweKwe::setString(std::string_view group, std::string_view key, std::string_view value) {
  m_groups[std::string(group)][std::string(key)] = std::string(value);
}

void KineticweKwe::setBool(std::string_view group, std::string_view key, bool value) {
  setString(group, key, value ? "true" : "false");
}

void KineticweKwe::setInt(std::string_view group, std::string_view key, int value) {
  setString(group, key, std::to_string(value));
}

void KineticweKwe::setDouble(std::string_view group, std::string_view key, double value) {
  std::ostringstream out;
  out.precision(6);
  out << value;
  setString(group, key, out.str());
}

void KineticweKwe::setStringList(std::string_view group, std::string_view key, const std::vector<std::string>& values) {
  std::string encoded;
  for (std::size_t i = 0; i < values.size(); ++i) {
    if (i > 0) {
      encoded.push_back(';');
    }
    encoded += escapeListItem(values[i]);
  }
  setString(group, key, encoded);
}

bool KineticweKwe::hasKey(std::string_view group, std::string_view key) const {
  const auto groupIt = m_groups.find(std::string(group));
  if (groupIt == m_groups.end()) {
    return false;
  }
  return groupIt->second.find(std::string(key)) != groupIt->second.end();
}

bool KineticweKwe::removeKey(std::string_view group, std::string_view key) {
  const auto groupIt = m_groups.find(std::string(group));
  if (groupIt == m_groups.end()) {
    return false;
  }
  const bool removed = groupIt->second.erase(std::string(key)) > 0;
  if (groupIt->second.empty()) {
    m_groups.erase(groupIt);
  }
  return removed;
}

std::vector<std::string> KineticweKwe::groupNames() const {
  std::vector<std::string> names;
  names.reserve(m_groups.size());
  for (const auto& [name, entries] : m_groups) {
    names.push_back(name);
  }
  return names;
}

bool KineticweKwe::save() const {
  const auto target = path();
  std::error_code error;
  std::filesystem::create_directories(target.parent_path(), error);
  if (error) {
    return false;
  }
  const auto tmpPath = target;
  std::string tmpName = tmpPath.string() + ".tmp";
  {
    std::ofstream out(tmpName);
    if (!out.is_open()) {
      return false;
    }
    for (const auto& [group, entries] : m_groups) {
      if (entries.empty()) {
        continue;
      }
      out << '[' << group << "]\n";
      for (const auto& [key, value] : entries) {
        out << key << '=' << value << '\n';
      }
      out << '\n';
    }
  }
  std::filesystem::rename(tmpName, target, error);
  return !error;
}