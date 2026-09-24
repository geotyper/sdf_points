#pragma once

// Pure capture helpers (no Vulkan, no processes) so they can be unit tested.

#include <cstdint>
#include <ctime>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace vkexp {

enum class VideoCodec {
    Hevc,   // macOS: hevc_videotoolbox; elsewhere: libx265
    ProRes, // prores_ks HQ in .mov, for editing
    H264,   // libx264
};

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

[[nodiscard]] VideoCodec defaultVideoCodec();
[[nodiscard]] std::string_view videoCodecName(VideoCodec codec);
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

// "1920x1080" -> {1920, 1080}; "framebuffer" -> {0, 0}.
[[nodiscard]] std::optional<CaptureResolution> parseCaptureResolution(std::string_view text);

// Searches a PATH-style list ("a:b:c"; ';' on Windows).
[[nodiscard]] std::optional<std::filesystem::path> findExecutable(std::string_view name,
                                                                  std::string_view searchPath);
// PATH plus the usual Homebrew / system locations: apps started from Finder
// do not inherit the shell PATH.
[[nodiscard]] std::optional<std::filesystem::path> findFfmpeg();

} // namespace vkexp
