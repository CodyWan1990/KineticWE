#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

struct KeyChord;

// Bridges the shell's KeyChord (XKB keysym + KeyMod bitmask) and the raw
// QKeySequence ints the org.kde.kglobalaccel daemon stores and compares
// (Qt::Key | Qt::KeyboardModifier bits). The tables mirror the compositor's
// own keysym<->Qt table (src/xkb.cpp) so the values we send over D-Bus agree
// exactly with what the embedded daemon receives from keyboard input.
namespace KeybindCodec {
  // Qt::KeyboardModifier bits as encoded in QKeySequence ints.
  inline constexpr std::uint32_t ShiftModifier = 0x02000000;
  inline constexpr std::uint32_t ControlModifier = 0x04000000;
  inline constexpr std::uint32_t AltModifier = 0x08000000;
  inline constexpr std::uint32_t MetaModifier = 0x10000000;
  inline constexpr std::uint32_t KeypadModifier = 0x20000000;

  // KeyChord -> single QKeySequence int ("Meta+V" -> 0x10000056). Letters are
  // upper-cased and, mirroring the compositor, a Shift held for a letter is
  // dropped (the daemon never sees it). Returns std::nullopt for chords the
  // daemon cannot represent.
  [[nodiscard]] std::optional<std::uint32_t> toQtKey(const KeyChord& chord);

  // QKeySequence int -> KeyChord. Returns std::nullopt for keys that have no
  // XKB keysym counterpart (should not happen for values from the daemon).
  [[nodiscard]] std::optional<KeyChord> fromQtKey(std::uint32_t key);

  // "Meta+V", "Ctrl+Shift+P", "F5" — KDE-style names, Meta first. Round-trips
  // with fromChordString for every chord toQtKey/fromQtKey can produce.
  [[nodiscard]] std::string toChordString(const KeyChord& chord);
  [[nodiscard]] std::optional<KeyChord> fromChordString(std::string_view spec);

  // True when the chord is a bare key (no modifiers). Used to keep
  // keybinds.kwe readable (e.g. "F5" instead of "Meta+F5" is unambiguous).
  [[nodiscard]] bool isBareChord(const KeyChord& chord) noexcept;
} // namespace KeybindCodec
