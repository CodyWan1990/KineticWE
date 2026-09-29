#include "capture/kwin_thumbnail_capture.h"

#include "capture/toplevel_thumbnail_capture.h"
#include "core/log.h"
#include "dbus/session_bus.h"

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <fcntl.h>
#include <map>
#include <memory>
#include <optional>
#include <poll.h>
#include <sdbus-c++/sdbus-c++.h>
#include <span>
#include <string>
#include <unistd.h>
#include <utility>
#include <vector>
#include <wayland-client-protocol.h>

struct KwinThumbnailCapturePending {
  int readFd = -1;
  int maxWidth = 1;
  int maxHeight = 1;
  bool cancelled = false;
  bool finished = false;
  ToplevelThumbnailSource::CompletionCallback onComplete;

  ~KwinThumbnailCapturePending() {
    if (readFd >= 0) {
      ::close(readFd);
    }
  }
};

namespace {
  constexpr Logger kLog("kwin_thumbnail_capture");

  const sdbus::ServiceName kScreenShotBusName{"org.kde.KWin.ScreenShot2"};
  const sdbus::ObjectPath kScreenShotPath{"/org/kde/KWin/ScreenShot2"};
  constexpr auto kScreenShotInterface = "org.kde.KWin.ScreenShot2";

  // QImage::Format values that ScreenShot2 reports in the "format" result field.
  constexpr std::uint32_t kQImageFormatRGB32 = 4;
  constexpr std::uint32_t kQImageFormatARGB32 = 5;
  constexpr std::uint32_t kQImageFormatARGB32Premultiplied = 6;
  constexpr std::uint32_t kQImageFormatRGBX8888 = 16;

  constexpr std::uint32_t kMaxDimension = 16384;

  // ScreenShot2 hands back a raw QImage whose byte order happens to match the
  // equivalent wl_shm format on little-endian machines, so makeToplevelThumbnail
  // can do the premultiply/scale work. Note that WL_SHM_FORMAT_ARGB8888 is 0, so
  // "unsupported" must be signalled separately from the format value.
  [[nodiscard]] std::optional<std::uint32_t> wlShmFormatForQImage(std::uint32_t format) {
    switch (format) {
    case kQImageFormatRGB32:
    case kQImageFormatRGBX8888:
      return WL_SHM_FORMAT_XRGB8888;
    case kQImageFormatARGB32:
    case kQImageFormatARGB32Premultiplied:
      return WL_SHM_FORMAT_ARGB8888;
    default:
      return std::nullopt;
    }
  }

  [[nodiscard]] std::optional<std::uint32_t>
  resultUint32(const std::map<std::string, sdbus::Variant>& results, const char* key) {
    const auto entry = results.find(key);
    if (entry == results.end()) {
      return std::nullopt;
    }
    try {
      return entry->second.get<std::uint32_t>();
    } catch (const sdbus::Error&) {
      return std::nullopt;
    }
  }

  [[nodiscard]] bool readExact(int fd, std::uint8_t* out, std::size_t size, std::chrono::milliseconds timeout) {
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    std::size_t have = 0;
    while (have < size) {
      const auto now = std::chrono::steady_clock::now();
      if (now >= deadline) {
        return false;
      }
      const long long remainingMs = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - now).count();
      pollfd descriptor{fd, POLLIN, 0};
      const int ready = ::poll(&descriptor, 1, static_cast<int>(std::clamp(remainingMs, 0LL, 5000LL)));
      if (ready < 0) {
        if (errno == EINTR) {
          continue;
        }
        return false;
      }
      if (ready == 0) {
        return false;
      }
      const ssize_t count = ::read(fd, out + have, size - have);
      if (count < 0) {
        if (errno == EINTR) {
          continue;
        }
        return false;
      }
      if (count == 0) {
        return false;
      }
      have += static_cast<std::size_t>(count);
    }
    return true;
  }

  [[nodiscard]] std::optional<ScreencopyImage> decodeScreenshot(
      const std::shared_ptr<KwinThumbnailCapturePending>& pending,
      const std::map<std::string, sdbus::Variant>& results, std::string& error
  ) {
    const auto type = results.find("type");
    if (type == results.end()) {
      error = "KWin screenshot reply is missing the image type";
      return std::nullopt;
    }
    try {
      if (type->second.get<std::string>() != "raw") {
        error = "unsupported KWin screenshot type";
        return std::nullopt;
      }
    } catch (const sdbus::Error&) {
      error = "invalid KWin screenshot type";
      return std::nullopt;
    }

    const auto width = resultUint32(results, "width");
    const auto height = resultUint32(results, "height");
    const auto stride = resultUint32(results, "stride");
    const auto format = resultUint32(results, "format");
    if (!width || !height || !stride || !format || *width == 0 || *height == 0 || *width > kMaxDimension
        || *height > kMaxDimension || *stride < *width * 4U || *stride > kMaxDimension * 4U) {
      error = "invalid KWin screenshot metadata";
      return std::nullopt;
    }
    const auto wlFormat = wlShmFormatForQImage(*format);
    if (!wlFormat.has_value()) {
      error = "unsupported KWin screenshot format";
      return std::nullopt;
    }

    // Bounded by kMaxDimension above, so this cannot overflow size_t.
    const auto expected = static_cast<std::size_t>(*stride) * *height;
    std::vector<std::uint8_t> raw(expected);
    if (!readExact(pending->readFd, raw.data(), raw.size(), std::chrono::milliseconds(2000))) {
      error = "failed to read KWin screenshot";
      return std::nullopt;
    }

    std::span<const std::uint8_t> pixels = raw;
    std::vector<std::uint8_t> packed;
    const std::size_t rowBytes = static_cast<std::size_t>(*width) * 4U;
    if (*stride != rowBytes) {
      packed.resize(rowBytes * *height);
      for (std::uint32_t y = 0; y < *height; ++y) {
        std::memcpy(
            packed.data() + static_cast<std::size_t>(y) * rowBytes, raw.data() + static_cast<std::size_t>(y) * *stride,
            rowBytes
        );
      }
      pixels = packed;
    }

    auto thumbnail = capture::makeToplevelThumbnail(
        pixels, static_cast<int>(*width), static_cast<int>(*height), *wlFormat, pending->maxWidth, pending->maxHeight,
        WL_OUTPUT_TRANSFORM_NORMAL
    );
    if (!thumbnail.has_value()) {
      error = "failed to decode KWin screenshot";
      return std::nullopt;
    }
    return thumbnail;
  }
} // namespace

KwinThumbnailCapture::KwinThumbnailCapture(SessionBus& bus) {
  try {
    m_proxy = sdbus::createProxy(bus.connection(), kScreenShotBusName, kScreenShotPath);
  } catch (const sdbus::Error& e) {
    kLog.debug("KWin ScreenShot2 proxy unavailable: {}", e.what());
  }
}

KwinThumbnailCapture::~KwinThumbnailCapture() { cancelInFlight(); }

bool KwinThumbnailCapture::available() const noexcept { return m_proxy != nullptr; }

bool KwinThumbnailCapture::busy() const noexcept {
  return m_pending != nullptr && !m_pending->finished && !m_pending->cancelled;
}

void KwinThumbnailCapture::capture(
    const ToplevelCaptureSource& source, int maxWidth, int maxHeight, CompletionCallback onComplete
) {
  if (busy() || m_proxy == nullptr || maxWidth <= 0 || maxHeight <= 0) {
    if (onComplete) {
      onComplete(
          std::nullopt,
          busy() ? "KWin thumbnail capture already in progress" : "KWin thumbnail capture unavailable"
      );
    }
    return;
  }
  if (source.windowId.empty()) {
    if (onComplete) {
      onComplete(std::nullopt, "window has no KWin UUID");
    }
    return;
  }

  int fds[2];
  if (::pipe2(fds, O_CLOEXEC) != 0) {
    if (onComplete) {
      onComplete(std::nullopt, "failed to create KWin screenshot pipe");
    }
    return;
  }

  auto pending = std::make_shared<KwinThumbnailCapturePending>();
  pending->readFd = fds[0];
  pending->maxWidth = maxWidth;
  pending->maxHeight = maxHeight;
  pending->onComplete = std::move(onComplete);
  m_pending = pending;

  std::map<std::string, sdbus::Variant> options;
  options.emplace("include-cursor", sdbus::Variant{false});
  options.emplace("include-decoration", sdbus::Variant{true});
  options.emplace("include-shadow", sdbus::Variant{false});
  options.emplace("native-resolution", sdbus::Variant{false});

  try {
    m_proxy->callMethodAsync("CaptureWindow")
        .onInterface(kScreenShotInterface)
        .withTimeout(std::chrono::milliseconds(2000))
        .withArguments(source.windowId, options, sdbus::UnixFd{fds[1]})
        .uponReplyInvoke(
            [pending](std::optional<sdbus::Error> error, std::map<std::string, sdbus::Variant> results) {
              if (pending->cancelled) {
                return;
              }
              std::optional<ScreencopyImage> image;
              std::string failure;
              if (error.has_value()) {
                failure = std::string("KWin screenshot failed: ") + error->what();
              } else {
                image = decodeScreenshot(pending, results, failure);
              }
              pending->finished = true;
              auto callback = std::exchange(pending->onComplete, {});
              if (callback) {
                callback(std::move(image), std::move(failure));
              }
            }
        );
  } catch (const sdbus::Error& e) {
    pending->cancelled = true;
    m_pending.reset();
    auto callback = std::exchange(pending->onComplete, {});
    if (callback) {
      callback(std::nullopt, "KWin screenshot dispatch failed: " + std::string(e.what()));
    }
  }
}

void KwinThumbnailCapture::cancelInFlight() {
  if (m_pending != nullptr) {
    m_pending->cancelled = true;
    m_pending.reset();
  }
}
