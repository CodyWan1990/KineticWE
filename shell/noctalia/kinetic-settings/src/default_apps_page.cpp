#include "default_apps_page.h"

#include "core/log.h"
#include "core/process/process.h"
#include "i18n/i18n.h"
#include "system/desktop_entry.h"
#include "ui/builders.h"
#include "ui/controls/button.h"
#include "ui/controls/flex.h"
#include "ui/controls/glyph.h"
#include "ui/controls/label.h"
#include "ui/controls/select.h"
#include "ui/controls/separator.h"
#include "ui/palette.h"
#include "ui/style.h"
#include "util/string_utils.h"

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <functional>
#include <optional>
#include <ranges>
#include <string>
#include <string_view>
#include <utility>

namespace {

  constexpr Logger kLog("kinetic-settings");

  constexpr int kSettingDescriptionMaxLines = 5;
  constexpr float kDefaultControlWidth = 230.0F;

  // The Default Applications page persists to the standard XDG mimeapps.list,
  // in the user's real config home — NOT the compositor-redirected
  // KWE_CONFIG_HOME. The shell process keeps the real XDG_CONFIG_HOME, so this
  // resolves to the shared config location that xdg-mime and KDE apps read.
  std::filesystem::path mimeappsListPath() {
    if (const char* xdg = std::getenv("XDG_CONFIG_HOME"); xdg != nullptr && *xdg != '\0') {
      return std::filesystem::path(xdg) / "mimeapps.list";
    }
    if (const char* home = std::getenv("HOME"); home != nullptr && *home != '\0') {
      return std::filesystem::path(home) / ".config" / "mimeapps.list";
    }
    return std::filesystem::path(".") / "mimeapps.list";
  }

  [[nodiscard]] std::unique_ptr<Label>
  makeLabel(std::string_view text, float fontSize, const ColorSpec& color, FontWeight fontWeight = FontWeight::Normal) {
    return ui::label({
        .text = std::string(text),
        .fontSize = fontSize,
        .fontWeight = fontWeight,
        .color = color,
    });
  }

  [[nodiscard]] std::unique_ptr<Flex>
  makeStatusBadge(std::string_view label, const ColorSpec& fill, const ColorSpec& color, float scale) {
    return ui::row(
        {
            .align = FlexAlign::Center,
            .gap = 0.0F,
            .paddingV = 0.0F,
            .paddingH = Style::spaceXs * scale,
            .fill = fill,
            .radius = Style::scaledRadiusSm(scale),
        },
        makeLabel(label, Style::fontSizeCaption * scale, color, FontWeight::Bold)
    );
  }

  [[nodiscard]] std::unique_ptr<Flex> makeOverrideBadge(float scale) {
    return makeStatusBadge(
        i18n::tr("settings.badges.override"), colorSpecFromRole(ColorRole::Primary, 0.15F),
        colorSpecFromRole(ColorRole::Primary), scale
    );
  }

  // Splits a semicolon-separated .desktop MimeType= value into its tokens.
  [[nodiscard]] std::vector<std::string> splitMimeKeys(std::string_view keys) {
    std::vector<std::string> out;
    std::size_t start = 0;
    while (start <= keys.size()) {
      const std::size_t end = keys.find(';', start);
      const std::size_t len = (end == std::string_view::npos) ? keys.size() - start : end - start;
      std::string token = StringUtils::trim(keys.substr(start, len));
      if (!token.empty()) {
        out.push_back(std::move(token));
      }
      if (end == std::string_view::npos) {
        break;
      }
      start = end + 1;
    }
    return out;
  }

  // Whether `entry` declares support for any of the semicolon-separated keys
  // in `keys`.
  [[nodiscard]] bool entrySupportsMimeKeys(const DesktopEntry& entry, std::string_view keys) {
    if (keys.empty()) {
      return false;
    }
    for (const std::string& want : splitMimeKeys(keys)) {
      for (const std::string& have : splitMimeKeys(entry.mimeTypes)) {
        if (have == want) {
          return true;
        }
      }
    }
    return false;
  }

  // The desktop-file id (e.g. "firefox.desktop") stored in mimeapps.list. The
  // shell's DesktopEntry::id is the file stem, so the ".desktop" suffix is
  // appended to match the id form KDE/xdg-mime use.
  [[nodiscard]] std::string desktopFileId(const DesktopEntry& entry) {
    return entry.id + ".desktop";
  }

} // namespace

// ---------------------------------------------------------------------------
// Schema: the field descriptor table — the single source of truth for the
// rows, their MIME keys, defaults and presentation.
// ---------------------------------------------------------------------------

const std::vector<DefaultAppsPage::Field>& DefaultAppsPage::fields() {
  static const std::vector<Field> table{
      Field{
          .title = "Web browser",
          .subtitle = "Opens web links and pages",
          .pageGroup = "Internet",
          .mimeKeys = "x-scheme-handler/http;x-scheme-handler/https",
      },
      Field{
          .title = "Email client",
          .subtitle = "Opens mailto: links and email",
          .pageGroup = "Internet",
          .mimeKeys = "x-scheme-handler/mailto",
      },
      Field{
          .title = "Calendar",
          .subtitle = "Opens calendar and subscription links",
          .pageGroup = "Internet",
          .advanced = true,
          .mimeKeys = "x-scheme-handler/webcal;x-scheme-handler/caldav",
      },
      Field{
          .title = "Phone numbers",
          .subtitle = "Opens telephone links",
          .pageGroup = "Internet",
          .advanced = true,
          .mimeKeys = "x-scheme-handler/tel",
      },
      Field{
          .title = "Image viewer",
          .subtitle = "Opens photos and other pictures",
          .pageGroup = "Multimedia",
          .mimeKeys = "image/png;image/jpeg;image/gif;image/webp;image/bmp",
      },
      Field{
          .title = "Music player",
          .subtitle = "Plays audio files",
          .pageGroup = "Multimedia",
          .mimeKeys = "audio/mpeg;audio/x-flac;audio/ogg;audio/wav",
      },
      Field{
          .title = "Video player",
          .subtitle = "Plays video files",
          .pageGroup = "Multimedia",
          .mimeKeys = "video/mp4;video/x-matroska;video/webm;video/ogg",
      },
      Field{
          .title = "Text editor",
          .subtitle = "Opens plain text documents",
          .pageGroup = "Documents",
          .mimeKeys = "text/plain",
      },
      Field{
          .title = "PDF viewer",
          .subtitle = "Opens PDF documents",
          .pageGroup = "Documents",
          .mimeKeys = "application/pdf",
      },
      Field{
          .title = "File manager",
          .subtitle = "Opens folders and the desktop",
          .pageGroup = "Utilities",
          .mimeKeys = "inode/directory",
      },
      Field{
          .title = "Terminal emulator",
          .subtitle = "Opens a command-line terminal",
          .pageGroup = "Utilities",
          .matchByTerminal = true,
      },
      Field{
          .title = "Archive manager",
          .subtitle = "Opens compressed archives",
          .pageGroup = "Utilities",
          .mimeKeys = "application/zip;application/x-tar;application/gzip;application/x-7z-compressed",
      },
      Field{
          .title = "Map",
          .subtitle = "Opens geographic links",
          .pageGroup = "Utilities",
          .advanced = true,
          .mimeKeys = "x-scheme-handler/geo",
      },
  };
  return table;
}

std::string DefaultAppsPage::keyPath(const Field& field) {
  // The path used for reset/override bookkeeping is just the row's title
  // (rows are not config-group keys, so a readable stable token is fine).
  return std::string(field.title);
}

const DefaultAppsPage::Field* DefaultAppsPage::findField(std::string_view path) {
  for (const auto& field : fields()) {
    if (path == keyPath(field)) {
      return &field;
    }
  }
  return nullptr;
}

// ---------------------------------------------------------------------------
// Values: effective state, override detection.
// ---------------------------------------------------------------------------

void DefaultAppsPage::ensureLoaded() {
  if (!m_loaded) {
    m_config.setPath(mimeappsListPath());
    m_config.load();
    m_loaded = true;
  }
}

DefaultAppsPage::Value DefaultAppsPage::fileBackedValue(const Field& field) const {
  // The effective default for a row is the first MIME key that has a default
  // application configured.
  for (const std::string& key : splitMimeKeys(field.mimeKeys)) {
    if (const auto v = m_config.readString("Default Applications", key); v.has_value() && !v->empty()) {
      return *v;
    }
  }
  return Value{};
}

DefaultAppsPage::Value DefaultAppsPage::effectiveValue(const Field& field) const {
  const auto staged = m_pending.find(keyPath(field));
  if (staged != m_pending.end()) {
    return staged->second;
  }
  return fileBackedValue(field);
}

bool DefaultAppsPage::overridden(const Field& field) const {
  return !effectiveValue(field).empty();
}

bool DefaultAppsPage::matchesFilter(const Field& field, const Filter& filter) const {
  if (!filter.showAdvanced && field.advanced) {
    return false;
  }
  if (filter.showOverriddenOnly && !overridden(field)) {
    return false;
  }
  if (!filter.searchQuery.empty()) {
    const auto matches = [&filter](std::string_view text) {
      return StringUtils::containsInsensitive(text, filter.searchQuery);
    };
    if (!matches(field.title) && !matches(field.subtitle) && !matches(field.pageGroup)) {
      return false;
    }
  }
  return true;
}

std::vector<const DesktopEntry*> DefaultAppsPage::candidateApps(const Field& field) const {
  std::vector<const DesktopEntry*> candidates;
  for (const DesktopEntry& entry : desktopEntries()) {
    if (field.matchByTerminal) {
      if (entry.terminal) {
        candidates.push_back(&entry);
      }
    } else if (entrySupportsMimeKeys(entry, field.mimeKeys)) {
      candidates.push_back(&entry);
    }
  }
  std::ranges::sort(
      candidates, [](const DesktopEntry* a, const DesktopEntry* b) { return a->name < b->name; }
  );
  return candidates;
}

// ---------------------------------------------------------------------------
// Persistence: staged edits + Apply.
// ---------------------------------------------------------------------------

void DefaultAppsPage::stage(const Field& field, const Value& value) {
  ensureLoaded();
  const std::string path = keyPath(field);
  if (value == fileBackedValue(field)) {
    m_pending.erase(path);
  } else {
    m_pending[path] = value;
  }
}

void DefaultAppsPage::stageAndRebuild(const Field& field, const Value& value, const Callbacks& callbacks) {
  stage(field, value);
  if (callbacks.requestRebuild) {
    callbacks.requestRebuild();
  }
}

bool DefaultAppsPage::applyAll() {
  ensureLoaded();
  if (m_pending.empty()) {
    return true;
  }

  // A non-empty value sets the first MIME key and clears the others the row
  // owns; "System" (empty) removes every key the row owns so the freedesktop
  // default applies.
  for (const auto& [path, value] : m_pending) {
    if (const Field* field = findField(path)) {
      const auto keys = splitMimeKeys(field->mimeKeys);
      bool wrote = false;
      for (const std::string& key : keys) {
        if (!wrote && !value.empty()) {
          m_config.setString("Default Applications", key, value);
          wrote = true;
        } else {
          m_config.removeKey("Default Applications", key);
        }
      }
    }
  }

  if (!m_config.save()) {
    m_errorNotice = "Failed to write mimeapps.list. Your changes were kept; try Apply again.";
    kLog.warn("kinetic-settings: failed to write {}", m_config.path().string());
    return false;
  }

  // Refresh the desktop/MIME caches so the new defaults take effect immediately
  // for every consumer, not just xdg-open: update-desktop-database rebuilds
  // mimeinfo.cache (used by some launchers) and kbuildsycoca rebuilds KDE's
  // KApplicationTrader cache (used by KDE apps to resolve default apps). Both
  // are no-ops when the tool isn't installed.
  if (process::commandExists("update-desktop-database")) {
    (void)process::runAsync("update-desktop-database");
  }
  if (process::commandExists("kbuildsycoca6")) {
    (void)process::runAsync("kbuildsycoca6");
  }

  m_pending.clear();
  m_errorNotice.clear();
  return true;
}

std::vector<std::string> DefaultAppsPage::resettableKeyPaths() const {
  std::vector<std::string> paths;
  paths.reserve(fields().size());
  for (const auto& field : fields()) {
    paths.push_back(keyPath(field));
  }
  return paths;
}

void DefaultAppsPage::resetKeyPath(std::string_view path) {
  if (const Field* field = findField(path)) {
    stage(*field, Value{});
  }
}

void DefaultAppsPage::resetAll() {
  for (const auto& field : fields()) {
    stage(field, Value{});
  }
}

// ---------------------------------------------------------------------------
// Controls + scene.
// ---------------------------------------------------------------------------

namespace {

  [[nodiscard]] std::unique_ptr<Button> makeResetButton(
      std::string path, bool pendingConfirmation, float scale, const DefaultAppsPage::Callbacks& callbacks
  ) {
    return ui::button({
        .text = i18n::tr(pendingConfirmation ? "settings.actions.confirm-reset" : "settings.actions.reset"),
        .fontSize = Style::fontSizeCaption * scale,
        .variant = pendingConfirmation ? ButtonVariant::Destructive : ButtonVariant::Ghost,
        .minHeight = Style::controlHeightSm * scale,
        .paddingV = Style::spaceXs * scale,
        .paddingH = Style::spaceSm * scale,
        .radius = Style::scaledRadiusMd(scale),
        .onClick = [path = std::move(path), pendingConfirmation, callbacks]() mutable {
          if (!pendingConfirmation) {
            if (callbacks.armReset) {
              callbacks.armReset(std::move(path));
            }
            return;
          }
          if (callbacks.confirmReset) {
            callbacks.confirmReset(std::move(path));
          }
        },
    });
  }

} // namespace

std::unique_ptr<Node>
DefaultAppsPage::buildControl(const Field& field, float scale, const Callbacks& callbacks) {
  // The first option is always "System" (empty value = use the system/XDG
  // default), followed by every app that can handle the row's MIME types.
  std::vector<std::string> labels;
  labels.emplace_back("System");
  const auto candidates = candidateApps(field);
  for (const DesktopEntry* app : candidates) {
    labels.push_back(app->name);
  }

  const Value current = effectiveValue(field);
  std::optional<std::size_t> selectedIndex;
  if (!current.empty()) {
    // Match the configured desktop-file id (e.g. "firefox.desktop") to a
    // candidate by its full id (id + ".desktop"). A name match covers entries
    // whose stored id no longer resolves to a candidate.
    for (std::size_t i = 0; i < candidates.size(); ++i) {
      if (desktopFileId(*candidates[i]) == current) {
        selectedIndex = i + 1; // +1 because "System" is option 0
        break;
      }
    }
    if (!selectedIndex.has_value()) {
      for (std::size_t i = 0; i < candidates.size(); ++i) {
        if (candidates[i]->name == current) {
          selectedIndex = i + 1;
          break;
        }
      }
    }
  }

  const bool known = selectedIndex.has_value();
  // Placeholder shown when a configured default doesn't resolve to an offered
  // app (e.g. the app was uninstalled). "System" (empty) never shows one.
  std::optional<std::string> placeholder;
  if (!known && !current.empty()) {
    placeholder = "Unknown app";
  }
  return ui::select({
      .options = labels,
      .selectedIndex = known ? std::optional<std::size_t>{*selectedIndex} : std::nullopt,
      .clearSelection = !known,
      .placeholder = placeholder,
      .fontSize = Style::fontSizeBody * scale,
      .controlHeight = Style::controlHeight * scale,
      .glyphSize = Style::fontSizeBody * scale,
      .width = kDefaultControlWidth * scale,
      .height = Style::controlHeight * scale,
      .onSelectionChanged = [this, field, callbacks, candidates](std::size_t index, std::string_view /*label*/) {
        // index 0 == "System" (empty value).
        const Value value = index == 0 ? Value{} : Value{desktopFileId(*candidates[index - 1])};
        stageAndRebuild(field, value, callbacks);
      },
  });
}

void DefaultAppsPage::addSettingRow(
    Flex& parent, const Field& field, float scale, std::string_view armedResetKey, const Callbacks& callbacks
) {
  const std::string path = keyPath(field);
  const bool isOverridden = overridden(field);

  auto titleRow = ui::row({.align = FlexAlign::Center, .gap = Style::spaceSm * scale, .fillWidth = true});
  titleRow->addChild(makeLabel(
      field.title, Style::fontSizeBody * scale, colorSpecFromRole(ColorRole::OnSurface), FontWeight::Bold
  ));
  titleRow->addChild(ui::spacer());

  auto copy = ui::column({.align = FlexAlign::Start, .gap = Style::spaceXs * scale, .flexGrow = 1.0F});
  copy->addChild(std::move(titleRow));
  if (!field.subtitle.empty()) {
    copy->addChild(ui::label({
        .text = std::string(field.subtitle),
        .fontSize = Style::fontSizeCaption * scale,
        .color = colorSpecFromRole(ColorRole::OnSurfaceVariant),
        .maxLines = kSettingDescriptionMaxLines,
    }));
  }

  auto actions = ui::row({.align = FlexAlign::Center, .gap = Style::spaceSm * scale});
  if (isOverridden) {
    actions->addChild(makeOverrideBadge(scale));
    actions->addChild(makeResetButton(path, armedResetKey == path, scale, callbacks));
  }
  actions->addChild(buildControl(field, scale, callbacks));

  auto row = ui::row(
      {.align = FlexAlign::Center,
       .justify = FlexJustify::SpaceBetween,
       .gap = Style::spaceXs * scale,
       .paddingV = 2.0F * scale,
       .fillWidth = true}
  );
  row->addChild(std::move(copy));
  row->addChild(std::move(actions));
  parent.addChild(std::move(row));
}

std::unique_ptr<Node> DefaultAppsPage::build(
    float scale, const Filter& filter, std::string_view armedResetKey, const Callbacks& callbacks
) {
  ensureLoaded();

  auto root = ui::column({
      .align = FlexAlign::Stretch,
      .gap = Style::spaceSm * scale,
      .padding = Style::spaceLg * scale,
      .fill = clearColorSpec(),
      .fillWidth = true,
  });

  root->addChild(ui::row(
      {.align = FlexAlign::Center, .gap = Style::spaceSm * scale},
      ui::glyph({
          .glyph = "apps",
          .glyphSize = Style::fontSizeHeader * scale,
          .color = colorSpecFromRole(ColorRole::Primary),
      }),
      makeLabel("Default Applications", Style::fontSizeHeader * scale, colorSpecFromRole(ColorRole::Primary), FontWeight::Bold)
  ));

  if (!m_errorNotice.empty()) {
    root->addChild(ui::row(
        {.align = FlexAlign::Center,
         .gap = Style::spaceSm * scale,
         .paddingV = Style::spaceXs * scale,
         .paddingH = Style::spaceMd * scale,
         .fill = colorSpecFromRole(ColorRole::Error, 0.15F),
         .radius = Style::scaledRadiusMd(scale)},
        makeLabel(m_errorNotice, Style::fontSizeCaption * scale, colorSpecFromRole(ColorRole::Error))
    ));
  }

  bool anyRows = false;
  bool firstGroup = true;
  std::string_view lastGroup;
  const auto addGroupHeader = [&](std::string_view title) {
    if (title.empty()) {
      return;
    }
    if (!firstGroup) {
      root->addChild(ui::column(
          {.align = FlexAlign::Stretch,
           .gap = Style::spaceSm * scale,
           .configure = [scale](Flex& flex) { flex.setPadding(Style::spaceSm * scale, 0.0F, 0.0F, 0.0F); }},
          ui::separator(),
          makeLabel(title, Style::fontSizeBody * scale, colorSpecFromRole(ColorRole::Secondary), FontWeight::Bold)
      ));
    } else {
      root->addChild(
          makeLabel(title, Style::fontSizeBody * scale, colorSpecFromRole(ColorRole::Secondary), FontWeight::Bold)
      );
    }
    firstGroup = false;
  };

  for (const auto& field : fields()) {
    if (!matchesFilter(field, filter)) {
      continue;
    }
    if (field.pageGroup != lastGroup) {
      lastGroup = field.pageGroup;
      addGroupHeader(lastGroup);
    }
    addSettingRow(*root, field, scale, armedResetKey, callbacks);
    anyRows = true;
  }

  if (!anyRows) {
    root->addChild(makeLabel(
        "No settings match the current filters.", Style::fontSizeBody * scale,
        colorSpecFromRole(ColorRole::OnSurfaceVariant)
    ));
  }

  return root;
}
