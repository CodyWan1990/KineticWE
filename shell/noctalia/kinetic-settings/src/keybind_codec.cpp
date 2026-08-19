#include "keybind_codec.h"

#include "core/input/key_chord.h"
#include "core/input/key_modifiers.h"

#include <xkbcommon/xkbcommon-keysyms.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <charconv>
#include <cstddef>
#include <string>
#include <utility>

namespace {

struct KeyDef {
  const char* name;   // canonical KDE-style name (parse is case-insensitive)
  std::uint32_t sym;  // XKB keysym
  std::uint32_t qt;   // Qt::Key value, no modifier bits
  bool keypad = false; // produced by a keypad key (adds KeypadModifier)
};

// Kept in sync with the compositor's g_rgSymXToQT (src/xkb.cpp), which is
// itself synced with qxkbcommon.cpp in Qt. Qt values from Qt 6 qnamespace.h.
constexpr std::array<KeyDef, 192> kKeyDefs = {{
    // misc keys
    {"Esc", XKB_KEY_Escape, 0x01000000},
    {"Tab", XKB_KEY_Tab, 0x01000001},
    {"Backtab", XKB_KEY_ISO_Left_Tab, 0x01000002},
    {"Backspace", XKB_KEY_BackSpace, 0x01000003},
    {"Return", XKB_KEY_Return, 0x01000004},
    {"Insert", XKB_KEY_Insert, 0x01000006},
    {"Delete", XKB_KEY_Delete, 0x01000007},
    {"Pause", XKB_KEY_Pause, 0x01000008},
    {"Print", XKB_KEY_Print, 0x01000009},
    {"SysReq", XKB_KEY_Sys_Req, 0x0100000a},
    {"Clear", XKB_KEY_Clear, 0x0100000b},
    // cursor movement
    {"Home", XKB_KEY_Home, 0x01000010},
    {"End", XKB_KEY_End, 0x01000011},
    {"Left", XKB_KEY_Left, 0x01000012},
    {"Up", XKB_KEY_Up, 0x01000013},
    {"Right", XKB_KEY_Right, 0x01000014},
    {"Down", XKB_KEY_Down, 0x01000015},
    {"PageUp", XKB_KEY_Prior, 0x01000016},
    {"PageDown", XKB_KEY_Next, 0x01000017},
    // locks
    {"CapsLock", XKB_KEY_Caps_Lock, 0x01000024},
    {"NumLock", XKB_KEY_Num_Lock, 0x01000025},
    {"ScrollLock", XKB_KEY_Scroll_Lock, 0x01000026},
    // extra keys
    {"Menu", XKB_KEY_Menu, 0x01000055},
    {"Help", XKB_KEY_Help, 0x01000058},
    {"Undo", XKB_KEY_Undo, 0x01000065},
    {"Redo", XKB_KEY_Redo, 0x01000066},
    {"Find", XKB_KEY_Find, 0x01000068},
    {"Cancel", XKB_KEY_Cancel, 0x01000069},
    // international
    {"AltGr", XKB_KEY_ISO_Level3_Shift, 0x01001103},
    {"MultiKey", XKB_KEY_Multi_key, 0x01001120},
    {"Codeinput", XKB_KEY_Codeinput, 0x01001137},
    {"ModeSwitch", XKB_KEY_Mode_switch, 0x0100117e},
    // keypad (keypad flag set: the compositor adds KeypadModifier for these)
    {"Numpad Space", XKB_KEY_KP_Space, 0x20, true},
    {"Numpad Tab", XKB_KEY_KP_Tab, 0x01000001, true},
    {"Numpad Enter", XKB_KEY_KP_Enter, 0x01000005, true},
    {"Numpad Home", XKB_KEY_KP_Home, 0x01000010, true},
    {"Numpad Left", XKB_KEY_KP_Left, 0x01000012, true},
    {"Numpad Up", XKB_KEY_KP_Up, 0x01000013, true},
    {"Numpad Right", XKB_KEY_KP_Right, 0x01000014, true},
    {"Numpad Down", XKB_KEY_KP_Down, 0x01000015, true},
    {"Numpad PageUp", XKB_KEY_KP_Prior, 0x01000016, true},
    {"Numpad PageDown", XKB_KEY_KP_Next, 0x01000017, true},
    {"Numpad End", XKB_KEY_KP_End, 0x01000011, true},
    {"Numpad Begin", XKB_KEY_KP_Begin, 0x0100000b, true},
    {"Numpad Insert", XKB_KEY_KP_Insert, 0x01000006, true},
    {"Numpad Delete", XKB_KEY_KP_Delete, 0x01000007, true},
    {"Numpad Equal", XKB_KEY_KP_Equal, 0x3d, true},
    {"Numpad Multiply", XKB_KEY_KP_Multiply, 0x2a, true},
    {"Numpad Add", XKB_KEY_KP_Add, 0x2b, true},
    {"Numpad Separator", XKB_KEY_KP_Separator, 0x2c, true},
    {"Numpad Subtract", XKB_KEY_KP_Subtract, 0x2d, true},
    {"Numpad Decimal", XKB_KEY_KP_Decimal, 0x2e, true},
    {"Numpad Divide", XKB_KEY_KP_Divide, 0x2f, true},
    // media keys
    {"Back", XKB_KEY_XF86Back, 0x01000061},
    {"Forward", XKB_KEY_XF86Forward, 0x01000062},
    {"Stop", XKB_KEY_XF86Stop, 0x01000063},
    {"Refresh", XKB_KEY_XF86Refresh, 0x01000064},
    {"VolumeDown", XKB_KEY_XF86AudioLowerVolume, 0x01000070},
    {"VolumeMute", XKB_KEY_XF86AudioMute, 0x01000071},
    {"VolumeUp", XKB_KEY_XF86AudioRaiseVolume, 0x01000072},
    {"MediaPlay", XKB_KEY_XF86AudioPlay, 0x01000080},
    {"MediaStop", XKB_KEY_XF86AudioStop, 0x01000081},
    {"MediaPrevious", XKB_KEY_XF86AudioPrev, 0x01000082},
    {"MediaNext", XKB_KEY_XF86AudioNext, 0x01000083},
    {"MediaRecord", XKB_KEY_XF86AudioRecord, 0x01000084},
    {"MediaPause", XKB_KEY_XF86AudioPause, 0x01000085},
    {"HomePage", XKB_KEY_XF86HomePage, 0x01000090},
    {"Favorites", XKB_KEY_XF86Favorites, 0x01000091},
    {"Search", XKB_KEY_XF86Search, 0x01000092},
    {"Standby", XKB_KEY_XF86Standby, 0x01000093},
    {"OpenUrl", XKB_KEY_XF86OpenURL, 0x01000094},
    {"LaunchMail", XKB_KEY_XF86Mail, 0x010000a0},
    {"LaunchMedia", XKB_KEY_XF86AudioMedia, 0x010000a1},
    {"Memo", XKB_KEY_XF86Memo, 0x010000bc},
    {"ToDoList", XKB_KEY_XF86ToDoList, 0x010000cc},
    {"Calendar", XKB_KEY_XF86Calendar, 0x010000e4},
    {"PowerDown", XKB_KEY_XF86PowerDown, 0x0100010b},
    {"ContrastAdjust", XKB_KEY_XF86ContrastAdjust, 0x0100010d},
    {"MonBrightnessUp", XKB_KEY_XF86MonBrightnessUp, 0x010000b2},
    {"MonBrightnessDown", XKB_KEY_XF86MonBrightnessDown, 0x010000b3},
    {"KeyboardLightOnOff", XKB_KEY_XF86KbdLightOnOff, 0x010000b4},
    {"KeyboardBrightnessUp", XKB_KEY_XF86KbdBrightnessUp, 0x010000b5},
    {"KeyboardBrightnessDown", XKB_KEY_XF86KbdBrightnessDown, 0x010000b6},
    {"PowerOff", XKB_KEY_XF86PowerOff, 0x010000b7},
    {"WakeUp", XKB_KEY_XF86WakeUp, 0x010000b8},
    {"Eject", XKB_KEY_XF86Eject, 0x010000b9},
    {"ScreenSaver", XKB_KEY_XF86ScreenSaver, 0x010000ba},
    {"WWW", XKB_KEY_XF86WWW, 0x010000bb},
    {"Sleep", XKB_KEY_XF86Sleep, 0x01020004},
    {"LightBulb", XKB_KEY_XF86LightBulb, 0x010000bd},
    {"Shop", XKB_KEY_XF86Shop, 0x010000be},
    {"History", XKB_KEY_XF86History, 0x010000bf},
    {"AddFavorite", XKB_KEY_XF86AddFavorite, 0x010000c0},
    {"HotLinks", XKB_KEY_XF86HotLinks, 0x010000c1},
    {"BrightnessAdjust", XKB_KEY_XF86BrightnessAdjust, 0x010000c2},
    {"Finance", XKB_KEY_XF86Finance, 0x010000c3},
    {"Community", XKB_KEY_XF86Community, 0x010000c4},
    {"AudioRewind", XKB_KEY_XF86AudioRewind, 0x010000c5},
    {"BackForward", XKB_KEY_XF86BackForward, 0x010000c6},
    {"ApplicationLeft", XKB_KEY_XF86ApplicationLeft, 0x010000c7},
    {"ApplicationRight", XKB_KEY_XF86ApplicationRight, 0x010000c8},
    {"Book", XKB_KEY_XF86Book, 0x010000c9},
    {"CD", XKB_KEY_XF86CD, 0x010000ca},
    {"Calculator", XKB_KEY_XF86Calculator, 0x010000cb},
    {"ClearGrab", XKB_KEY_XF86ClearGrab, 0x010000cd},
    {"Close", XKB_KEY_XF86Close, 0x010000ce},
    {"Copy", XKB_KEY_XF86Copy, 0x010000cf},
    {"Cut", XKB_KEY_XF86Cut, 0x010000d0},
    {"Display", XKB_KEY_XF86Display, 0x010000d1},
    {"DOS", XKB_KEY_XF86DOS, 0x010000d2},
    {"Documents", XKB_KEY_XF86Documents, 0x010000d3},
    {"Excel", XKB_KEY_XF86Excel, 0x010000d4},
    {"Explorer", XKB_KEY_XF86Explorer, 0x010000d5},
    {"Game", XKB_KEY_XF86Game, 0x010000d6},
    {"Go", XKB_KEY_XF86Go, 0x010000d7},
    {"iTouch", XKB_KEY_XF86iTouch, 0x010000d8},
    {"LogOff", XKB_KEY_XF86LogOff, 0x010000d9},
    {"Market", XKB_KEY_XF86Market, 0x010000da},
    {"Meeting", XKB_KEY_XF86Meeting, 0x010000db},
    {"MenuKB", XKB_KEY_XF86MenuKB, 0x010000dc},
    {"MenuPB", XKB_KEY_XF86MenuPB, 0x010000dd},
    {"MySites", XKB_KEY_XF86MySites, 0x010000de},
    {"News", XKB_KEY_XF86News, 0x010000df},
    {"OfficeHome", XKB_KEY_XF86OfficeHome, 0x010000e0},
    {"New", XKB_KEY_XF86New, 0x01000120},
    {"Open", XKB_KEY_XF86Open, 0x01000121},
    {"Option", XKB_KEY_XF86Option, 0x010000e1},
    {"Paste", XKB_KEY_XF86Paste, 0x010000e2},
    {"Phone", XKB_KEY_XF86Phone, 0x010000e3},
    {"Reply", XKB_KEY_XF86Reply, 0x010000e5},
    {"Reload", XKB_KEY_XF86Reload, 0x010000e6},
    {"RotateWindows", XKB_KEY_XF86RotateWindows, 0x010000e7},
    {"RotationPB", XKB_KEY_XF86RotationPB, 0x010000e8},
    {"RotationKB", XKB_KEY_XF86RotationKB, 0x010000e9},
    {"Save", XKB_KEY_XF86Save, 0x010000ea},
    {"Send", XKB_KEY_XF86Send, 0x010000eb},
    {"Spell", XKB_KEY_XF86Spell, 0x010000ec},
    {"SplitScreen", XKB_KEY_XF86SplitScreen, 0x010000ed},
    {"Support", XKB_KEY_XF86Support, 0x010000ee},
    {"TaskPane", XKB_KEY_XF86TaskPane, 0x010000ef},
    {"Terminal", XKB_KEY_XF86Terminal, 0x010000f0},
    {"Tools", XKB_KEY_XF86Tools, 0x010000f1},
    {"Travel", XKB_KEY_XF86Travel, 0x010000f2},
    {"Video", XKB_KEY_XF86Video, 0x010000f3},
    {"Word", XKB_KEY_XF86Word, 0x010000f4},
    {"Xfer", XKB_KEY_XF86Xfer, 0x010000f5},
    {"ZoomIn", XKB_KEY_XF86ZoomIn, 0x010000f6},
    {"ZoomOut", XKB_KEY_XF86ZoomOut, 0x010000f7},
    {"Away", XKB_KEY_XF86Away, 0x010000f8},
    {"Messenger", XKB_KEY_XF86Messenger, 0x010000f9},
    {"WebCam", XKB_KEY_XF86WebCam, 0x010000fa},
    {"MailForward", XKB_KEY_XF86MailForward, 0x010000fb},
    {"Pictures", XKB_KEY_XF86Pictures, 0x010000fc},
    {"Music", XKB_KEY_XF86Music, 0x010000fd},
    {"Battery", XKB_KEY_XF86Battery, 0x010000fe},
    {"Bluetooth", XKB_KEY_XF86Bluetooth, 0x010000ff},
    {"WLAN", XKB_KEY_XF86WLAN, 0x01000100},
    {"UWB", XKB_KEY_XF86UWB, 0x01000101},
    {"AudioForward", XKB_KEY_XF86AudioForward, 0x01000102},
    {"AudioRepeat", XKB_KEY_XF86AudioRepeat, 0x01000103},
    {"AudioRandomPlay", XKB_KEY_XF86AudioRandomPlay, 0x01000104},
    {"Subtitle", XKB_KEY_XF86Subtitle, 0x01000105},
    {"AudioCycleTrack", XKB_KEY_XF86AudioCycleTrack, 0x01000106},
    {"Time", XKB_KEY_XF86Time, 0x01000107},
    {"Hibernate", XKB_KEY_XF86Hibernate, 0x01000108},
    {"View", XKB_KEY_XF86View, 0x01000109},
    {"TopMenu", XKB_KEY_XF86TopMenu, 0x0100010a},
    {"Suspend", XKB_KEY_XF86Suspend, 0x0100010c},
    {"TouchpadToggle", XKB_KEY_XF86TouchpadToggle, 0x01000110},
    {"TouchpadOn", XKB_KEY_XF86TouchpadOn, 0x01000111},
    {"TouchpadOff", XKB_KEY_XF86TouchpadOff, 0x01000112},
    {"MicMute", XKB_KEY_XF86AudioMicMute, 0x01000113},
    {"Red", XKB_KEY_XF86Red, 0x01000114},
    {"Green", XKB_KEY_XF86Green, 0x01000115},
    {"Yellow", XKB_KEY_XF86Yellow, 0x01000116},
    {"Blue", XKB_KEY_XF86Blue, 0x01000117},
    {"Launch0", XKB_KEY_XF86Launch0, 0x010000a2},
    {"Launch1", XKB_KEY_XF86Launch1, 0x010000a3},
    {"Launch2", XKB_KEY_XF86Launch2, 0x010000a4},
    {"Launch3", XKB_KEY_XF86Launch3, 0x010000a5},
    {"Launch4", XKB_KEY_XF86Launch4, 0x010000a6},
    {"Launch5", XKB_KEY_XF86Launch5, 0x010000a7},
    {"Launch6", XKB_KEY_XF86Launch6, 0x010000a8},
    {"Launch7", XKB_KEY_XF86Launch7, 0x010000a9},
    {"Launch8", XKB_KEY_XF86Launch8, 0x010000aa},
    {"Launch9", XKB_KEY_XF86Launch9, 0x010000ab},
    {"LaunchA", XKB_KEY_XF86LaunchA, 0x010000ac},
    {"LaunchB", XKB_KEY_XF86LaunchB, 0x010000ad},
    {"LaunchC", XKB_KEY_XF86LaunchC, 0x010000ae},
    {"LaunchD", XKB_KEY_XF86LaunchD, 0x010000af},
    {"LaunchE", XKB_KEY_XF86LaunchE, 0x010000b0},
    {"LaunchF", XKB_KEY_XF86LaunchF, 0x010000b1},
}};

// Function keys: XKB F1..F35 (0xffbe..0xffe0) -> Qt Key_F1..Key_F35.
constexpr std::uint32_t kXkbF1 = 0xffbe;
constexpr std::uint32_t kQtF1 = 0x01000030;
constexpr std::uint32_t kXkbFCount = 35;

// Keypad range the compositor marks with KeypadModifier.
constexpr std::uint32_t kXkbKpFirst = XKB_KEY_KP_Space; // 0xff80
constexpr std::uint32_t kXkbKpLast = XKB_KEY_KP_Equal;  // 0xffbd

[[nodiscard]] std::uint32_t upperAscii(std::uint32_t sym) noexcept {
  return (sym >= 0x61 && sym <= 0x7a) ? sym - 0x20 : sym;
}

[[nodiscard]] std::optional<std::uint32_t> symToQt(std::uint32_t sym) noexcept {
  if (sym >= 0x20 && sym <= 0x7e) {
    return upperAscii(sym);
  }
  if (sym >= kXkbF1 && sym < kXkbF1 + kXkbFCount) {
    return kQtF1 + (sym - kXkbF1);
  }
  const auto it = std::ranges::find_if(kKeyDefs, [sym](const KeyDef& def) { return def.sym == sym; });
  if (it != kKeyDefs.end()) {
    return it->qt;
  }
  return std::nullopt;
}

// Keypad-aware reverse lookup: KeypadModifier selects the Numpad variant.
[[nodiscard]] std::optional<std::uint32_t> qtToSym(std::uint32_t qt, bool keypad) noexcept {
  if (qt >= kQtF1 && qt < kQtF1 + kXkbFCount) {
    return kXkbF1 + (qt - kQtF1);
  }
  if (qt >= 0x20 && qt <= 0x7e) {
    return qt;
  }
  const auto it = std::ranges::find_if(kKeyDefs, [qt, keypad](const KeyDef& def) {
    return def.qt == qt && def.keypad == keypad;
  });
  if (it != kKeyDefs.end()) {
    return it->sym;
  }
  return std::nullopt;
}

[[nodiscard]] const KeyDef* findByName(std::string_view name) noexcept {
  const auto it = std::ranges::find_if(kKeyDefs, [name](const KeyDef& def) {
    if (def.name == nullptr) {
      return false;
    }
    std::string_view a(def.name);
    if (a.size() != name.size()) {
      return false;
    }
    for (std::size_t i = 0; i < a.size(); ++i) {
      if (std::tolower(static_cast<unsigned char>(a[i])) != std::tolower(static_cast<unsigned char>(name[i]))) {
        return false;
      }
    }
    return true;
  });
  return it == kKeyDefs.end() ? nullptr : &*it;
}

[[nodiscard]] std::string_view nameOfSym(std::uint32_t sym) noexcept {
  if (sym >= kXkbF1 && sym < kXkbF1 + kXkbFCount) {
    return {}; // handled by caller
  }
  const auto it = std::ranges::find_if(kKeyDefs, [sym](const KeyDef& def) { return def.sym == sym; });
  return it == kKeyDefs.end() ? std::string_view{} : std::string_view(it->name);
}

[[nodiscard]] std::optional<KeyChord> parseHexKey(std::string_view token) {
  if (token.size() < 3 || token[0] != '0' || (token[1] != 'x' && token[1] != 'X')) {
    return std::nullopt;
  }
  std::uint32_t value = 0;
  const auto [ptr, ec] = std::from_chars(token.data() + 2, token.data() + token.size(), value, 16);
  if (ec != std::errc{} || ptr != token.data() + token.size()) {
    return std::nullopt;
  }
  KeyChord chord;
  chord.sym = value;
  return chord;
}

[[nodiscard]] bool eqCi(std::string_view a, std::string_view b) noexcept {
  if (a.size() != b.size()) {
    return false;
  }
  for (std::size_t i = 0; i < a.size(); ++i) {
    if (std::tolower(static_cast<unsigned char>(a[i])) != std::tolower(static_cast<unsigned char>(b[i]))) {
      return false;
    }
  }
  return true;
}

} // namespace

namespace KeybindCodec {

std::optional<std::uint32_t> toQtKey(const KeyChord& chord) {
  const auto qt = symToQt(chord.sym);
  if (!qt.has_value()) {
    return std::nullopt;
  }
  std::uint32_t key = *qt;
  if (chord.sym >= kXkbKpFirst && chord.sym <= kXkbKpLast) {
    key |= KeypadModifier;
  }
  if ((chord.modifiers & KeyMod::Shift) != 0) {
    key |= ShiftModifier;
  }
  if ((chord.modifiers & KeyMod::Ctrl) != 0) {
    key |= ControlModifier;
  }
  if ((chord.modifiers & KeyMod::Alt) != 0) {
    key |= AltModifier;
  }
  if ((chord.modifiers & KeyMod::Super) != 0) {
    key |= MetaModifier;
  }
  return key;
}

std::optional<KeyChord> fromQtKey(std::uint32_t key) {
  const bool keypad = (key & KeypadModifier) != 0;
  const auto sym = qtToSym(key & ~0x3f000000U, keypad);
  if (!sym.has_value()) {
    return std::nullopt;
  }
  KeyChord chord;
  chord.sym = *sym;
  if ((key & ShiftModifier) != 0) {
    chord.modifiers |= KeyMod::Shift;
  }
  if ((key & ControlModifier) != 0) {
    chord.modifiers |= KeyMod::Ctrl;
  }
  if ((key & AltModifier) != 0) {
    chord.modifiers |= KeyMod::Alt;
  }
  if ((key & MetaModifier) != 0) {
    chord.modifiers |= KeyMod::Super;
  }
  return chord;
}

std::string toChordString(const KeyChord& chord) {
  std::string result;
  const auto append = [&result](std::string_view part) {
    if (!result.empty()) {
      result += '+';
    }
    result += part;
  };
  if ((chord.modifiers & KeyMod::Super) != 0) {
    append("Meta");
  }
  if ((chord.modifiers & KeyMod::Ctrl) != 0) {
    append("Ctrl");
  }
  if ((chord.modifiers & KeyMod::Alt) != 0) {
    append("Alt");
  }
  if ((chord.modifiers & KeyMod::Shift) != 0) {
    append("Shift");
  }
  const auto name = nameOfSym(chord.sym);
  if (!name.empty()) {
    append(name);
  } else if (chord.sym >= kXkbF1 && chord.sym < kXkbF1 + kXkbFCount) {
    append("F" + std::to_string(chord.sym - kXkbF1 + 1));
  } else if (chord.sym >= 0x20 && chord.sym <= 0x7e) {
    append(std::string(1, static_cast<char>(upperAscii(chord.sym))));
  } else {
    char hex[16];
    const auto [ptr, ec] = std::to_chars(std::begin(hex), std::end(hex), chord.sym, 16);
    if (ec == std::errc{}) {
      append("0x" + std::string(std::begin(hex), ptr));
    }
  }
  return result;
}

std::optional<KeyChord> fromChordString(std::string_view spec) {
  KeyChord chord;
  bool sawKey = false;
  std::size_t start = 0;
  while (start <= spec.size()) {
    const auto end = spec.find('+', start);
    const auto part = spec.substr(start, end == std::string_view::npos ? spec.size() - start : end - start);
    if (!part.empty()) {
      const auto token = [&part]() {
        std::string trimmed(part);
        while (!trimmed.empty() && std::isspace(static_cast<unsigned char>(trimmed.front()))) {
          trimmed.erase(trimmed.begin());
        }
        while (!trimmed.empty() && std::isspace(static_cast<unsigned char>(trimmed.back()))) {
          trimmed.pop_back();
        }
        return trimmed;
      }();
      if (!token.empty()) {
        if (eqCi(token, "Meta") || eqCi(token, "Super")) {
          chord.modifiers |= KeyMod::Super;
        } else if (eqCi(token, "Ctrl") || eqCi(token, "Control")) {
          chord.modifiers |= KeyMod::Ctrl;
        } else if (eqCi(token, "Alt")) {
          chord.modifiers |= KeyMod::Alt;
        } else if (eqCi(token, "Shift")) {
          chord.modifiers |= KeyMod::Shift;
        } else {
          if (sawKey) {
            return std::nullopt;
          }
          sawKey = true;
          if (const auto* def = findByName(token); def != nullptr) {
            chord.sym = def->sym;
          } else if (token.size() == 1 && token[0] >= 0x20 && token[0] <= 0x7e) {
            chord.sym = upperAscii(static_cast<std::uint32_t>(static_cast<unsigned char>(token[0])));
          } else if (const auto hex = parseHexKey(token); hex.has_value()) {
            chord.sym = hex->sym;
          } else {
            return std::nullopt;
          }
        }
      }
    }
    if (end == std::string_view::npos) {
      break;
    }
    start = end + 1;
  }
  if (!sawKey || chord.sym == 0) {
    return std::nullopt;
  }
  return chord;
}

bool isBareChord(const KeyChord& chord) noexcept { return chord.modifiers == 0; }

} // namespace KeybindCodec