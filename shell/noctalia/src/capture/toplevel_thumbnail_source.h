#pragma once

#include "capture/screencopy_capture.h"

#include <cstdint>
#include <functional>
#include <optional>
#include <string>

// Identifies the compositor-specific source used to capture a window thumbnail.
struct ToplevelCaptureSource {
  // ext-foreign-toplevel handle, used by compositors that expose the ext protocols.
  std::uintptr_t extHandle = 0;
  // KWin internal window UUID, used by the org.kde.KWin.ScreenShot2 backend.
  std::string windowId;
};

// Common interface for window-thumbnail capture backends.
class ToplevelThumbnailSource {
public:
  using CompletionCallback = std::function<void(std::optional<ScreencopyImage>, std::string error)>;

  virtual ~ToplevelThumbnailSource() = default;

  [[nodiscard]] virtual bool available() const noexcept = 0;
  [[nodiscard]] virtual bool busy() const noexcept = 0;
  virtual void
  capture(const ToplevelCaptureSource& source, int maxWidth, int maxHeight, CompletionCallback onComplete) = 0;
  virtual void cancelInFlight() = 0;
};
