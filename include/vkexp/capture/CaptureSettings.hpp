#pragma once

// Pure capture helpers (no Vulkan, no processes) so they can be unit tested.

#include <array>
#include <cstdint>
#include <ctime>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace vkexp {

// The scene is made of 1-3 px coloured dots on black. 4:2:0 chroma (colour at
// half resolution) averages each dot's colour with the black around it, which
// visibly desaturates the video, so the default keeps more chroma.
enum class VideoCodec {
    Hevc,       // 4:2:2 10-bit; macOS: hevc_videotoolbox, elsewhere: libx265 4:4:4
    Hevc420,    // 4:2:0 8-bit HEVC for phones and browsers
    ProRes,     // prores_ks HQ 4:2:2 in .mov, for editing
    ProRes4444, // prores_ks 4444 in .mov: full chroma, largest files
    H264,       // libx264 4:2:0, plays everywhere
};

inline constexpr std::array allVideoCodecs{VideoCodec::Hevc, VideoCodec::Hevc420,
                                           VideoCodec::ProRes, VideoCodec::ProRes4444,
                                           VideoCodec::H264};

enum class RawPixelFormat { Rgba, Bgra };

struct VideoEncodeSettings {
    std::uint32_t width{};
    std::uint32_t height{};
    std::uint32_t fps{60};
    RawPixelFormat pixelFormat{RawPixelFormat::Bgra};
    VideoCodec codec{VideoCodec::H264};
    std::filesystem::path output;
};

struct CaptureResolution {
    std::uint32_t width{};
    std::uint32_t height{};
};

struct CaptureSize {
    enum class Mode {
        Framebuffer, // the window framebuffer
        Viewport,    // the Viewport panel: its size and proportions, times viewportScale
        Fixed,       // an explicit width x height
    };
    Mode mode{Mode::Framebuffer};
    CaptureResolution fixed{};
    std::uint32_t viewportScale{1};
};

[[nodiscard]] VideoCodec defaultVideoCodec();
[[nodiscard]] std::string_view videoCodecName(VideoCodec codec);
// One line for the UI: colour fidelity and where the file plays.
[[nodiscard]] std::string_view videoCodecDescription(VideoCodec codec);
[[nodiscard]] std::optional<VideoCodec> parseVideoCodec(std::string_view name);
[[nodiscard]] std::string_view videoFileExtension(VideoCodec codec);
[[nodiscard]] std::string_view ffmpegPixelFormat(RawPixelFormat format);

// Arguments after the executable; raw frames are read from stdin.
[[nodiscard]] std::vector<std::string> ffmpegArguments(const VideoEncodeSettings& settings);
[[nodiscard]] std::string shellQuote(std::string_view argument);
[[nodiscard]] std::string shellCommand(const std::filesystem::path& executable,
                                       const std::vector<std::string>& arguments);

// "sdf_points_YYYYMMDD_HHMMSS<extension>"
[[nodiscard]] std::string captureFileName(const std::tm& time, std::string_view extension);
// Appends _2, _3, ... when a file of the same second already exists.
[[nodiscard]] std::filesystem::path uniqueCapturePath(const std::filesystem::path& directory,
                                                      const std::tm& time,
                                                      std::string_view extension);
[[nodiscard]] std::tm localTimeNow();

// "framebuffer", "viewport", "viewport2x" (1x..4x) or "WxH" (64..4096).
[[nodiscard]] std::optional<CaptureSize> parseCaptureSize(std::string_view text);
// Final capture size: even (4:2:0 encoders), within 64..4096; oversized requests
// shrink uniformly so the aspect ratio is kept.
[[nodiscard]] CaptureResolution resolveCaptureSize(const CaptureSize& size,
                                                   CaptureResolution framebuffer,
                                                   CaptureResolution viewport);

// Searches a PATH-style list ("a:b:c"; ';' on Windows).
[[nodiscard]] std::optional<std::filesystem::path> findExecutable(std::string_view name,
                                                                  std::string_view searchPath);
// PATH plus the usual Homebrew / system locations: apps started from Finder
// do not inherit the shell PATH.
[[nodiscard]] std::optional<std::filesystem::path> findFfmpeg();

} // namespace vkexp
