#include "core/input/key_chord.h"
#include "core/input/key_modifiers.h"
#include "kinetic-settings/src/keybind_codec.h"
#include "test_check.h"

#include <string>
#include <string_view>

namespace {

  // Parse then re-serialize: catches name<->keysym gaps such as function keys,
  // which toChordString() emits as "F9" but fromChordString() must accept.
  void expectRoundTrip(std::string_view spec) {
    const auto chord = KeybindCodec::fromChordString(spec);
    TEST_CHECK(chord.has_value());
    TEST_CHECK(KeybindCodec::toChordString(*chord) == std::string(spec));
  }

} // namespace

int main() {
  // Function keys: the parser must cover the whole "F1".."F35" range.
  expectRoundTrip("F1");
  expectRoundTrip("F9");
  expectRoundTrip("F12");
  expectRoundTrip("F35");
  expectRoundTrip("Meta+Shift+F9");
  expectRoundTrip("Ctrl+F5");

  // Case-insensitive, matching findByName() and the rest of the format.
  TEST_CHECK(KeybindCodec::fromChordString("meta+shift+f9") == KeybindCodec::fromChordString("Meta+Shift+F9"));

  // F-key chords survive the daemon's integer encoding unchanged.
  const auto f9 = KeybindCodec::fromChordString("Meta+Shift+F9");
  TEST_CHECK(f9.has_value());
  const auto qt = KeybindCodec::toQtKey(*f9);
  TEST_CHECK(qt.has_value());
  const auto decoded = KeybindCodec::fromQtKey(*qt);
  TEST_CHECK(decoded.has_value());
  TEST_CHECK(KeybindCodec::toChordString(*decoded) == "Meta+Shift+F9");

  // Out-of-range or malformed tokens stay unrepresentable.
  TEST_CHECK(!KeybindCodec::fromChordString("F0").has_value());
  TEST_CHECK(!KeybindCodec::fromChordString("F36").has_value());
  TEST_CHECK(!KeybindCodec::fromChordString("Fn").has_value());

  return 0;
}
