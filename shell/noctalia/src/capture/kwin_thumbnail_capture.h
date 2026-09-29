#pragma once

#include "capture/toplevel_thumbnail_source.h"

#include <memory>
#include <string>

class SessionBus;
struct KwinThumbnailCapturePending;
namespace sdbus {
  class IProxy;
}

// Window thumbnails on KWin/KineticWE through the org.kde.KWin.ScreenShot2 D-Bus
// interface. KWin does not expose the ext-foreign-toplevel / ext-image-copy-capture
// protocols, but it can screenshot a window by its internal UUID, which the KWin
// script integration already reports as the compositor window id.
class KwinThumbnailCapture final : public ToplevelThumbnailSource {
public:
  explicit KwinThumbnailCapture(SessionBus& bus);
  ~KwinThumbnailCapture() override;

  KwinThumbnailCapture(const KwinThumbnailCapture&) = delete;
  KwinThumbnailCapture& operator=(const KwinThumbnailCapture&) = delete;

  [[nodiscard]] bool available() const noexcept override;
  [[nodiscard]] bool busy() const noexcept override;
  void capture(const ToplevelCaptureSource& source, int maxWidth, int maxHeight, CompletionCallback onComplete)
      override;
  void cancelInFlight() override;

private:
  std::unique_ptr<sdbus::IProxy> m_proxy;
  std::shared_ptr<KwinThumbnailCapturePending> m_pending;
};
