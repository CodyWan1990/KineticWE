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

// An installed window decoration theme. Only Aurorae themes are discovered
// (the kind downloadable from store.kde.org category 717); their directory name
// is the theme id used with the Aurorae KDecoration plugin.
struct DecorationThemeInfo {
  std::string name;        // theme directory name (Aurorae theme id)
  std::string displayName; // Name= from metadata.desktop, fallback to directory name
  std::string path;        // absolute path to the theme directory
  // Titlebar layout from the theme's own <name>rc (Aurorae's configrc), used by
  // Kinetic Settings to draw a faithful window preview. The defaults mirror
  // Aurorae's when the rc omits the keys: centered title, no side buttons, and
  // a 24px titlebar.
  std::string titleAlignment = "center"; // [General] TitleAlignment, lowercased
  std::string leftButtons;               // [General] LeftButtons (KDE button letters)
  std::string rightButtons;              // [General] RightButtons
  int titlebarHeight = 24;               // [Layout] TitleHeight + TitleEdgeTop + TitleEdgeBottom
};

// All installed icon themes, sorted by display name (case-insensitive).
// Each entry's path is the theme's root directory.
[[nodiscard]] std::vector<ThemeInfo> iconThemes();

// All installed cursor themes, sorted by display name (case-insensitive).
  [[nodiscard]] std::vector<ThemeInfo> cursorThemes();

  // All installed Aurorae window decoration themes, sorted by display name
  // (case-insensitive). Each path is the theme's directory under
  // `<data dir>/aurorae/themes/`.
  [[nodiscard]] std::vector<DecorationThemeInfo> decorationThemes();

  // Path to a theme's button artwork for a KDE/Aurorae button letter
  // (X close, I minimize, A maximize, H help, S alldesktops, F keepabove,
  // B keepbelow). Empty when the letter has no artwork or the theme ships none
  // (only uncompressed .svg is resolved; .svgz archives are skipped).
  [[nodiscard]] std::string decorationButtonFile(const std::string& themePath, char letter);

  // The window decoration themes root directory (where downloaded themes are
  // installed), preferring the user's own dir (`~/.local/share/aurorae/themes`)
  // and falling back to the first existing system root. Empty when nothing can
  // be resolved.
  [[nodiscard]] std::string decorationRootPath();

  // Creates (if needed) the user's Aurorae themes directory and opens it in the
  // default file manager. Returns true when a launch was attempted.
  bool openDecorationRoot();

  // The XDG icon theme root directory (where icon and cursor themes live),
  // preferring the user's own theme dir (~/.local/share/icons, ~/.icons) and
  // falling back to the first existing system root. Used by the settings pages
  // to open the folder in a file manager. Empty when nothing can be resolved.
  [[nodiscard]] std::string iconRootPath();

  // Opens the icon/cursor themes root directory in the user's default file
  // manager. Resolves the handler for inode/directory from mimeapps.list (the
  // file the Default Apps settings page writes), launches it with the directory
  // as argument, and only falls back to xdg-open when no default is configured.
  // Returns true when a launch was attempted.
  bool openIconRoot();

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