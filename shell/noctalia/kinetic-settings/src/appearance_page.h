#pragma once

#include "kinetic-settings/src/compositor_dbus.h"
#include "kinetic-settings/src/kineticwe_kwe.h"

#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

class Node;
class Renderer;
class SessionBus;

// The Appearance -> Title Bars settings page: the titlebar (window decoration)
// picker, rendered as a tile grid holding the four Kinetic decoration variants
// (standard/dots x dark/light), plus the Kinetic titlebar metrics (title bar
// height, title font size, button size) and the left/right title bar button
// selection (ButtonsOnLeft/ButtonsOnRight). The selection and metrics are written
// to the compositor's ~/.config/kineticwe/kineticwe.kwe ([org.kde.kdecoration2]
// library/theme + the Kinetic* metric keys) by applyAll(), which the window's
// Apply button calls; the compositor is then asked to reconfigure.
//
// Theme COLORS are not handled here: the shell's theme service owns the
// generated palette (qt5ct/qt6ct color schemes, ~/.config/kdeglobals, the GTK
// dark/light preference), so this page intentionally has no theme selector.
//
// The plain toggles and sliders are described by the fields() table (the same
// pattern as WindowBehaviorPage); adding one is a single table entry. The
// decoration tile grid and the ButtonsOnLeft/Right letter columns don't fit a
// table and keep their own staged values.
//
// Like the Tiling/Workspaces pages, edits are STAGED: a tile click updates the
// in-memory pending decoration and nothing touches disk until the window's
// Apply button calls applyAll(). On failure the staged edit is kept so Apply can
// be retried. The Icons and Cursors subcategories live in their own pages
// (IconsPage/CursorsPage) rendered by the owning window, not this page.
class AppearancePage {
public:
  struct Filter {
    std::string searchQuery;
    bool showOverriddenOnly = false;
  };

  // Glue back to the owning window (same shape as TilingPage/WorkspacesPage).
  struct Callbacks {
    std::function<void()> requestRebuild;
    std::function<void(std::string keyPath)> armReset;
    std::function<void(std::string keyPath)> confirmReset;
  };

  explicit AppearancePage(SessionBus& bus);

  std::unique_ptr<Node> build(
      float scale, Renderer& renderer, const Filter& filter, std::string_view armedResetKey,
      const Callbacks& callbacks
  );

  [[nodiscard]] std::vector<std::string> resettableKeyPaths() const;
  void resetKeyPath(std::string_view keyPath);
  void resetAll();

  [[nodiscard]] bool hasPending() const noexcept {
    return !m_pending.empty() || m_pendingDecoration.has_value() || m_pendingButtonsOnLeft.has_value()
        || m_pendingButtonsOnRight.has_value();
  }
  // Writes the staged titlebar selection and "Toggle title bars" state to
  // kineticwe.kwe and asks the compositor to reconfigure. Returns false on
  // failure (the staged edit is kept for retry).
  bool applyAll();

  void reloadFromDisk() noexcept {
    m_loaded = false;
    m_pending.clear();
    m_pendingDecoration.reset();
    m_pendingButtonsOnLeft.reset();
    m_pendingButtonsOnRight.reset();
    m_errorNotice.clear();
  }

  // Error message to surface when the last Apply failed (kept until the next
  // successful Apply or reload).
  [[nodiscard]] std::string_view lastError() const noexcept { return m_errorNotice; }

private:
  using Value = std::variant<bool, int>;

  enum class ControlKind {
    Toggle,
    Slider,
  };

  // The card a table row renders in.
  enum class Card {
    TitleBars, // the "Show title bars" card above the tile grid
    Size,      // "Title bar size"
    Buttons,   // "Title bar buttons", above the letter columns
  };

  struct Field {
    std::string_view group; // .kwe group
    std::string_view key;   // .kwe key
    ControlKind control = ControlKind::Toggle;
    std::string_view title;
    std::string_view subtitle; // empty = compact row (caption label, no description)
    Card card = Card::TitleBars;
    int minValue = 0; // sliders
    int maxValue = 100;
    std::string_view valueSuffix;
    // Bool stored as an int 1/0 instead of true/false.
    bool boolAsInt = false;
    // Compiled-in default; its type (bool/int) is the field's value type. A
    // value equal to it removes the key on Apply.
    Value fallback;
  };

  [[nodiscard]] static const std::vector<Field>& fields();
  [[nodiscard]] static std::string keyPath(const Field& field);
  [[nodiscard]] static const Field* findField(std::string_view keyPath);
  [[nodiscard]] static std::string_view cardTitle(Card card);
  // Value read from the config file (ignoring staged edits).
  [[nodiscard]] Value fileBackedValue(const Field& field) const;
  // The staged value when present, otherwise the file value.
  [[nodiscard]] Value effectiveValue(const Field& field) const;
  [[nodiscard]] bool overridden(const Field& field) const;
  [[nodiscard]] bool matchesFilter(const Field& field, const Filter& filter) const;
  // Stages an edit; un-stages when it matches the value already in the file.
  void stage(const Field& field, const Value& value);
  // Writes `value` into the config model (or removes the key at the default).
  void writeValue(const Field& field, const Value& value);
  [[nodiscard]] std::unique_ptr<Node>
  buildFieldRow(const Field& field, float scale, std::string_view armedResetKey, const Callbacks& callbacks);

  // The titlebar selection: a (library, theme) pair written to kineticwe.kwe
  // [org.kde.kdecoration2]. The Kinetic decoration is the compiled-in default.
  struct DecorationSelection {
    std::string library; // "kineticwe"
    std::string theme;   // "dark" / "light", or "dots-dark" / "dots-light"

    [[nodiscard]] bool operator==(const DecorationSelection& other) const noexcept {
      return library == other.library && theme == other.theme;
    }
  };

  void ensureLoaded();

  // --- Title bars (the page's only section) --------------------------------
  [[nodiscard]] static DecorationSelection defaultDecoration();
  [[nodiscard]] static std::string decorationKeyPath() { return "org.kde.kdecoration2.theme"; }
  [[nodiscard]] static std::string buttonsOnLeftKeyPath() { return "org.kde.kdecoration2.ButtonsOnLeft"; }
  [[nodiscard]] static std::string buttonsOnRightKeyPath() { return "org.kde.kdecoration2.ButtonsOnRight"; }
  [[nodiscard]] DecorationSelection fileBackedDecoration() const;
  [[nodiscard]] DecorationSelection effectiveDecoration() const;
  [[nodiscard]] bool overriddenDecoration() const;
  // Stages a decoration selection; clears the staged value when it matches disk.
  void stageDecoration(const DecorationSelection& selection);
  void stageDecorationAndRebuild(
      const DecorationSelection& selection, const Callbacks& callbacks
  );
  // Title bar buttons: the KDE letter-code run the compositor reads from
  // [org.kde.kdecoration2] ButtonsOnLeft/ButtonsOnRight. A value equal to the
  // compiled-in default clears the pending edit (and removes the key on Apply).
  [[nodiscard]] std::string buttonsOnLeftOnDisk() const;
  [[nodiscard]] std::string buttonsOnRightOnDisk() const;
  [[nodiscard]] std::string effectiveButtonsOnLeft() const;
  [[nodiscard]] std::string effectiveButtonsOnRight() const;
  [[nodiscard]] bool overriddenButtonsOnLeft() const;
  [[nodiscard]] bool overriddenButtonsOnRight() const;
  void stageButtonsOnLeft(std::string value);
  void stageButtonsOnRight(std::string value);
  // Inserts (on) or removes (off) a single letter in a button string, preserving
  // the order of the other letters.
  [[nodiscard]] static std::string toggleButton(std::string_view current, char letter, bool on);
  // Writes the staged titlebar selection and table edits to kineticwe.kwe
  // ([org.kde.kdecoration2] + [Windows]) and asks the compositor to reconfigure.
  // Reloads the file first so concurrently applied edits on other pages are
  // preserved.
  [[nodiscard]] bool applyTitleBars();
  // Builds the section (the toggle row, then the header/reset row and tile
  // grid). Returns nullptr when the active filter leaves nothing to show.
  [[nodiscard]] std::unique_ptr<Node> buildDecorationsSection(
      float scale, const Filter& filter, std::string_view armedResetKey, const Callbacks& callbacks
  );

  bool m_loaded = false;
  // Table edits staged by the controls, keyed by "group.key".
  std::map<std::string, Value> m_pending;
  // Staged titlebar selection (kineticwe.kwe [org.kde.kdecoration2]).
  std::optional<DecorationSelection> m_pendingDecoration;
  std::optional<std::string> m_pendingButtonsOnLeft;
  std::optional<std::string> m_pendingButtonsOnRight;
  // kineticwe.kwe reader/writer for the decoration keys.
  KineticweKwe m_decorationsConfig;
  CompositorDbus m_dbus;
  std::string m_errorNotice;
};
