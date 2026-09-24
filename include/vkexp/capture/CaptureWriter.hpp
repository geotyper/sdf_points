#pragma once

#include "vkexp/capture/CaptureSettings.hpp"

#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <deque>
#include <filesystem>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace vkexp {

// Writes captured frames on a dedicated thread: raw video to an ffmpeg pipe
// and PNG screenshots. The queue is bounded; producers block when it is full
// so no frame is ever dropped (a smooth fixed-step video beats a high fps).
class CaptureWriter {
public:
    using Pixels = std::vector<std::uint8_t>;

    struct Status {
        std::size_t queueDepth{};
        std::size_t queueCapacity{};
        std::uint64_t framesWritten{};
        bool failed{};
        std::string error;
        std::filesystem::path lastFile;
    };

    explicit CaptureWriter(std::size_t queueCapacity = 6);
    ~CaptureWriter();

    CaptureWriter(const CaptureWriter&) = delete;
    CaptureWriter& operator=(const CaptureWriter&) = delete;

    // Starts ffmpeg (popen) with `command`; later frames go to its stdin.
    // Returns false and sets `error` if the process cannot be started.
    [[nodiscard]] bool openVideo(const std::string& command, const std::filesystem::path& output,
                                 std::string& error);
    // Queues the pipe close after the frames already queued; never blocks.
    void closeVideo();

    // A recycled buffer of `bytes` bytes (contents unspecified).
    [[nodiscard]] Pixels acquireBuffer(std::size_t bytes);
    // Both return the milliseconds spent blocked on a full queue.
    double pushVideoFrame(Pixels pixels);
    double pushScreenshot(Pixels pixels, std::uint32_t width, std::uint32_t height,
                          RawPixelFormat format, std::filesystem::path output);

    // Time the writer thread spent writing since the previous call.
    [[nodiscard]] double takeWriteMilliseconds();
    [[nodiscard]] Status status() const;
    // Finishes every queued job (including the pipe close) and joins the thread.
    void shutdown();

private:
    struct Job {
        enum class Kind { OpenVideo, VideoFrame, Screenshot, CloseVideo };
        Kind kind{};
        Pixels pixels;
        std::uint32_t width{};
        std::uint32_t height{};
        RawPixelFormat format{};
        std::filesystem::path output;
        std::FILE* pipe{};
    };

    double push(Job job);
    void run();
    void process(Job& job);
    void fail(std::string message);
    void recycle(Pixels pixels);

    const std::size_t capacity_;
    mutable std::mutex mutex_;
    std::condition_variable workAvailable_;
    std::condition_variable spaceAvailable_;
    std::deque<Job> queue_;
    std::size_t queuedFrames_{};
    std::vector<Pixels> freeBuffers_;
    Status status_;
    bool stopping_{};
    std::atomic<std::int64_t> writeNanoseconds_{};
    // Owned by the writer thread.
    std::FILE* pipe_{};
    std::filesystem::path videoPath_;
    std::uint64_t videoFrames_{};
    bool videoFailed_{};
    std::thread thread_;
};

} // namespace vkexp
