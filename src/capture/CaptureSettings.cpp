#include "vkexp/capture/CaptureSettings.hpp"

#include <algorithm>
#include <charconv>
#include <cstdlib>
#include <system_error>

namespace vkexp {
namespace {

#ifdef _WIN32
constexpr char pathListSeparator = ';';
constexpr std::string_view executableSuffix = ".exe";
#else
constexpr char pathListSeparator = ':';
constexpr std::string_view executableSuffix;
#endif

bool isExecutableFile(const std::filesystem::path& path) {
    std::error_code error;
    if (!std::filesystem::is_regular_file(path, error)) {
        return false;
    }
#ifdef _WIN32
    return true;
#else
    const auto permissions = std::filesystem::status(path, error).permissions();
    using std::filesystem::perms;
    return !error && (permissions & (perms::owner_exec | perms::group_exec | perms::others_exec)) !=
                         perms::none;
#endif
}

std::optional<std::uint32_t> parseUnsigned(const std::string_view text) {
    std::uint32_t value = 0;
    const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value);
    if (error != std::errc{} || end != text.data() + text.size()) {
        return std::nullopt;
    }
    return value;
}

} // namespace

VideoCodec defaultVideoCodec() {
#ifdef __APPLE__
    return VideoCodec::Hevc;
#else
    return VideoCodec::H264;
#endif
}

std::string_view videoCodecName(const VideoCodec codec) {
    switch (codec) {
    case VideoCodec::Hevc:
        return "hevc";
    case VideoCodec::ProRes:
        return "prores";
    case VideoCodec::H264:
        return "h264";
    }
    return "h264";
}

std::optional<VideoCodec> parseVideoCodec(const std::string_view name) {
    for (const VideoCodec codec : {VideoCodec::Hevc, VideoCodec::ProRes, VideoCodec::H264}) {
        if (name == videoCodecName(codec)) {
            return codec;
        }
    }
    return std::nullopt;
}

std::string_view videoFileExtension(const VideoCodec codec) {
    return codec == VideoCodec::ProRes ? ".mov" : ".mp4";
}

std::string_view ffmpegPixelFormat(const RawPixelFormat format) {
    return format == RawPixelFormat::Bgra ? "bgra" : "rgba";
}

std::vector<std::string> ffmpegArguments(const VideoEncodeSettings& settings) {
    std::vector<std::string> arguments{
        "-hide_banner",
        "-loglevel",
        "warning",
        "-y",
        "-f",
        "rawvideo",
        "-pix_fmt",
        std::string{ffmpegPixelFormat(settings.pixelFormat)},
        "-video_size",
        std::to_string(settings.width) + "x" + std::to_string(settings.height),
        "-framerate",
        std::to_string(settings.fps),
        "-i",
        "-",
    };
    const auto append = [&arguments](std::initializer_list<std::string_view> values) {
        arguments.insert(arguments.end(), values.begin(), values.end());
    };
    // Frames are sRGB-encoded RGB; convert with BT.709 (not swscale's BT.601
    // default) and tag the stream so players decode the same colours.
    const std::string_view yuvFormat =
        settings.codec == VideoCodec::ProRes ? "yuv422p10le" : "yuv420p";
    arguments.emplace_back("-vf");
    arguments.push_back("scale=out_color_matrix=bt709:out_range=tv,format=" +
                        std::string{yuvFormat});
    switch (settings.codec) {
    case VideoCodec::Hevc:
#ifdef __APPLE__
        append({"-c:v", "hevc_videotoolbox", "-q:v", "65"});
#else
        append({"-c:v", "libx265", "-crf", "18", "-preset", "slow"});
#endif
        append({"-tag:v", "hvc1"});
        break;
    case VideoCodec::ProRes:
        append({"-c:v", "prores_ks", "-profile:v", "3", "-vendor", "apl0"});
        break;
    case VideoCodec::H264:
        append({"-c:v", "libx264", "-crf", "16", "-preset", "slow"});
        break;
    }
    append({"-colorspace", "bt709", "-color_primaries", "bt709", "-color_trc", "bt709",
            "-color_range", "tv"});
    if (settings.codec != VideoCodec::ProRes) {
        append({"-movflags", "+faststart"});
    }
    arguments.push_back(settings.output.string());
    return arguments;
}

std::string shellQuote(const std::string_view argument) {
#ifdef _WIN32
    std::string quoted{"\""};
    for (const char character : argument) {
        if (character == '"') {
            quoted += '\\';
        }
        quoted += character;
    }
    return quoted + "\"";
#else
    // Single quotes disable every expansion; embedded quotes become '\''.
    std::string quoted{"'"};
    for (const char character : argument) {
        if (character == '\'') {
            quoted += "'\\''";
        } else {
            quoted += character;
        }
    }
    return quoted + "'";
#endif
}

std::string shellCommand(const std::filesystem::path& executable,
                         const std::vector<std::string>& arguments) {
    std::string command = shellQuote(executable.string());
    for (const std::string& argument : arguments) {
        command += ' ';
        command += shellQuote(argument);
    }
    return command;
}

std::string captureFileName(const std::tm& time, const std::string_view extension) {
    char stamp[32]{};
    std::strftime(stamp, sizeof(stamp), "%Y%m%d_%H%M%S", &time);
    return "sdf_points_" + std::string{stamp} + std::string{extension};
}

std::filesystem::path uniqueCapturePath(const std::filesystem::path& directory,
                                        const std::tm& time, const std::string_view extension) {
    const std::string name = captureFileName(time, extension);
    std::filesystem::path candidate = directory / name;
    const std::string stem = name.substr(0, name.size() - extension.size());
    std::error_code error;
    for (int suffix = 2; std::filesystem::exists(candidate, error); ++suffix) {
        candidate = directory / (stem + "_" + std::to_string(suffix) + std::string{extension});
    }
    return candidate;
}

std::tm localTimeNow() {
    const std::time_t now = std::time(nullptr);
    std::tm local{};
#ifdef _WIN32
    localtime_s(&local, &now);
#else
    localtime_r(&now, &local);
#endif
    return local;
}

std::optional<CaptureSize> parseCaptureSize(const std::string_view text) {
    CaptureSize size;
    if (text == "framebuffer") {
        return size;
    }
    if (text.substr(0, 8) == "viewport") {
        size.mode = CaptureSize::Mode::Viewport;
        const std::string_view scale = text.substr(8);
        if (scale.empty()) {
            return size;
        }
        if (scale.size() < 2 || scale.back() != 'x') {
            return std::nullopt;
        }
        const auto factor = parseUnsigned(scale.substr(0, scale.size() - 1));
        if (!factor || *factor < 1 || *factor > 4) {
            return std::nullopt;
        }
        size.viewportScale = *factor;
        return size;
    }
    const std::size_t separator = text.find('x');
    if (separator == std::string_view::npos) {
        return std::nullopt;
    }
    const auto width = parseUnsigned(text.substr(0, separator));
    const auto height = parseUnsigned(text.substr(separator + 1));
    if (!width || !height || *width < 64 || *height < 64 || *width > 4096 || *height > 4096) {
        return std::nullopt;
    }
    size.mode = CaptureSize::Mode::Fixed;
    size.fixed = {*width, *height};
    return size;
}

CaptureResolution resolveCaptureSize(const CaptureSize& size, const CaptureResolution framebuffer,
                                     const CaptureResolution viewport) {
    static constexpr std::uint64_t minimum = 64;
    static constexpr std::uint64_t maximum = 4096;
    std::uint64_t width = framebuffer.width;
    std::uint64_t height = framebuffer.height;
    if (size.mode == CaptureSize::Mode::Viewport) {
        width = std::uint64_t{viewport.width} * size.viewportScale;
        height = std::uint64_t{viewport.height} * size.viewportScale;
    } else if (size.mode == CaptureSize::Mode::Fixed) {
        width = size.fixed.width;
        height = size.fixed.height;
    }
    const std::uint64_t longest = std::max(width, height);
    if (longest > maximum) {
        width = width * maximum / longest;
        height = height * maximum / longest;
    }
    const auto finish = [](const std::uint64_t value) {
        return static_cast<std::uint32_t>(std::clamp(value, minimum, maximum)) & ~1U;
    };
    return {finish(width), finish(height)};
}

std::optional<std::filesystem::path> findExecutable(const std::string_view name,
                                                    const std::string_view searchPath) {
    const std::string fileName = std::string{name} + std::string{executableSuffix};
    std::size_t begin = 0;
    while (begin <= searchPath.size()) {
        std::size_t end = searchPath.find(pathListSeparator, begin);
        if (end == std::string_view::npos) {
            end = searchPath.size();
        }
        const std::string_view directory = searchPath.substr(begin, end - begin);
        if (!directory.empty()) {
            const std::filesystem::path candidate = std::filesystem::path{directory} / fileName;
            if (isExecutableFile(candidate)) {
                return candidate;
            }
        }
        begin = end + 1;
    }
    return std::nullopt;
}

std::optional<std::filesystem::path> findFfmpeg() {
    std::string searchPath;
    if (const char* path = std::getenv("PATH"); path != nullptr) {
        searchPath = path;
    }
#ifndef _WIN32
    searchPath += ":/opt/homebrew/bin:/usr/local/bin:/usr/bin";
#endif
    return findExecutable("ffmpeg", searchPath);
}

} // namespace vkexp
