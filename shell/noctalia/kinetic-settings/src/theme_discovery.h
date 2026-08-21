#pragma once

#include <cstdint>
#include <string>
#include <vector>

// Enumerates the icon themes and cursor themes installed on the system (XDG
// icon theme directories) for the KineticWE settings Icons/Cursors pages.
// Both icon and cursor themes live under the same XDG icon roots; they are
// distinguished by their content: an icon theme exposes size subdirectories
// (scalable/, NNxNN/...), a cursor theme exposes a cursors/ (or
// cursors_scalable/) subdirectory.
//
// Self-contained: only uses the C++ standard library and the XDG environment,
// so it works identically in the compositor- and shell-side processes.
namespace theme_discovery {

struct ThemeInfo {
  std::string name;        // directory name (the value stored in config, e.g. "Papirus")
  std::string displayName; // Name= from index.theme, fallback to directory name
  std::string path;        // absolute path to the theme directory
};

// All installed icon themes, sorted by display name (case-insensitive).
// Each entry's path is the theme's root directory.
[[nodiscard]] std::vector<ThemeInfo> iconThemes();

// All installed cursor themes, sorted by display name (case-insensitive).
[[nodiscard]] std::vector<ThemeInfo> cursorThemes();

// Resolve a well-known icon name (e.g. "folder", "start-here") inside a
// specific theme directory. Returns the best candidate path (SVG preferred,
// then the largest PNG). Empty when the theme provides none of the names.
[[nodiscard]] std::string resolveIconInTheme(const std::string& themePath, const std::string& iconName);

// The path to a representative cursor file inside a cursor theme directory
  // (prefers "left_ptr", falling back to "default" and then any file in
  // cursors/). Empty when the theme has no usable cursor file.
  [[nodiscard]] std::string cursorPreviewFile(const std::string& themePath);

  // Decodes an Xcursor file into straight RGBA pixels for a preview. Returns
  // false when the file is not a valid Xcursor image. width/height/stride are
  // set on success; the pixel buffer is tightly packed (stride == width * 4).
  [[nodiscard]] bool decodeXcursorImage(
      const std::string& filePath, std::vector<std::uint8_t>& rgba, int& width, int& height
  );

} // namespace theme_discovery