#pragma once

#include <string_view>

class SessionBus;

// Mirrors the shell's font family into kineticwe.kwe
// ([org.kde.kdecoration2] KineticTitleFontFamily) so the Kinetic window
// decoration can paint captions in the same typeface. The key is always kept
// in sync; whether the decoration uses it is the user's
// KineticTitleFollowShellFont toggle (Appearance page). Only writes the file
// and reconfigures the compositor when the stored family actually differs, so
// it is cheap to call on every config reload.
void syncTitleFontFamily(std::string_view shellFontFamily, SessionBus& bus);
