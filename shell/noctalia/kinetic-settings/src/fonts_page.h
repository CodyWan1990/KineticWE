#pragma once

#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

class ConfigService;
class Flex;
class Node;

// The Fonts settings page: the shell interface typeface plus the generic
// sans-serif/serif/monospace families fontconfig resolves for other
// applications.
//
// The interface font is persisted as a shell.font_family override in the
// shell's settings file through ConfigService, so it takes effect immediately
// (the shell reloads on the mutation). The generic families are written to the
// user's fontconfig conf.d directory (99-kineticwe.conf), because fontconfig's
// standard user include loads it for every fontconfig consumer; the shell does
// not rewrite fonts.conf.
//
// Like the other pages, edits are STAGED and only written by the window's Apply
// button. A failed apply keeps the pending edits so Apply can be retried.
class FontsPage {
public:
  struct Filter {
    std::string searchQuery;
    bool showAdvanced = false;
    bool showOverriddenOnly = false;
  };

  struct Callbacks {
    std::function<void()> requestRebuild;
    std::function<void(std::string keyPath)> armReset;
    std::function<void(std::string keyPath)> confirmReset;
  };

  explicit FontsPage(ConfigService& config);

  std::unique_ptr<Node>
  build(float scale, const Filter& filter, std::string_view armedResetKey, const Callbacks& callbacks);

  [[nodiscard]] std::vector<std::string> resettableKeyPaths() const;
  void resetKeyPath(std::string_view keyPath);
  void resetAll();

  [[nodiscard]] bool hasPending() const noexcept;
  bool applyAll();

  void reloadFromDisk() noexcept;
  [[nodiscard]] std::string_view lastError() const noexcept { return m_errorNotice; }
  [[nodiscard]] bool hasOpenPicker() const noexcept { return !m_pickerTarget.empty(); }
  void cancelPicker() noexcept { m_pickerTarget.clear(); }

private:
  // Effective interface family ("" = the system default, sans-serif).
  [[nodiscard]] std::string interfaceValue() const;
  [[nodiscard]] std::string interfaceFamily() const;
  [[nodiscard]] std::string interfaceButtonLabel() const;
  [[nodiscard]] bool interfaceOverridden() const;

  // Effective generic-family alias ("" = the system default).
  [[nodiscard]] std::string aliasValue(std::string_view generic) const;
  [[nodiscard]] std::string aliasFamily(std::string_view generic) const;
  [[nodiscard]] std::string aliasButtonLabel(std::string_view generic) const;
  [[nodiscard]] bool aliasOverridden(std::string_view generic) const;

  void ensureAliasesLoaded();
  void stageInterface(std::string value);
  void stageAlias(std::string generic, std::string value);
  // Writes (or removes, when every alias is empty) the managed fontconfig
  // file. Returns false with m_errorNotice set on failure.
  [[nodiscard]] bool writeAliases(const std::map<std::string, std::string>& aliases);

  [[nodiscard]] std::unique_ptr<Node>
  buildInterfaceRow(float scale, std::string_view armedResetKey, const Callbacks& callbacks);
  [[nodiscard]] std::unique_ptr<Node> buildAliasRow(
      std::string_view generic, float scale, std::string_view armedResetKey, const Callbacks& callbacks
  );
  [[nodiscard]] std::unique_ptr<Node> buildFontPicker(
      float scale, std::string_view target, std::string_view selectedValue, const Callbacks& callbacks
  );

  ConfigService& m_config;
  std::optional<std::string> m_pendingInterface;
  std::map<std::string, std::string> m_pendingAliases;
  std::map<std::string, std::string> m_aliases;
  bool m_aliasesLoaded = false;
  std::string m_pickerTarget;
  std::string m_errorNotice;
};
