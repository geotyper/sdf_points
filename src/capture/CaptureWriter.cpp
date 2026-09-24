#include "vkexp/capture/CaptureWriter.hpp"

#include <stb_image_write.h>

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <csignal>
#include <cstring>
#include <iostream>
#include <utility>

#ifdef _WIN32
#define VKEXP_POPEN _popen
#define VKEXP_PCLOSE _pclose
#else
#include <sys/wait.h>
#define VKEXP_POPEN popen
#define VKEXP_PCLOSE pclose
#endif

namespace vkexp {
namespace {

#ifdef _WIN32
constexpr const char* pipeMode = "wb";
#else
constexpr const char* pipeMode = "w";
#endif

using Clock = std::chrono::steady_clock;

double millisecondsSince(const Clock::time_point started) {
    return std::chrono::duration<double, std::milli>(Clock::now() - started).count();
}

// Returns an empty string on a clean exit, otherwise a description.
std::string describeCloseStatus(const int status) {
    if (status == -1) {
        return std::string{"pclose failed: "} + std::strerror(errno);
    }
#ifdef _WIN32
    return status == 0 ? std::string{} : "ffmpeg exited with code " + std::to_string(status);
#else
    if (WIFEXITED(status)) {
        const int code = WEXITSTATUS(status);
        return code == 0 ? std::string{} : "ffmpeg exited with code " + std::to_string(code);
    }
    if (WIFSIGNALED(status)) {
        return "ffmpeg was killed by signal " + std::to_string(WTERMSIG(status));
    }
    return "ffmpeg ended abnormally";
#endif
}

} // namespace

CaptureWriter::CaptureWriter(const std::size_t queueCapacity)
    : capacity_(std::max<std::size_t>(queueCapacity, 1)) {
#ifndef _WIN32
    // A crashed ffmpeg must surface as a write error instead of killing the app.
    std::signal(SIGPIPE, SIG_IGN);
#endif
    status_.queueCapacity = capacity_;
    thread_ = std::thread([this] { run(); });
}

CaptureWriter::~CaptureWriter() { shutdown(); }

bool CaptureWriter::openVideo(const std::string& command, const std::filesystem::path& output,
                              std::string& error) {
    std::FILE* pipe = VKEXP_POPEN(command.c_str(), pipeMode);
    if (pipe == nullptr) {
        error = std::string{"Unable to start ffmpeg: "} + std::strerror(errno);
        return false;
    }
    {
        const std::lock_guard lock(mutex_);
        status_.failed = false;
        status_.error.clear();
        status_.framesWritten = 0;
    }
    Job job;
    job.kind = Job::Kind::OpenVideo;
    job.pipe = pipe;
    job.output = output;
    push(std::move(job));
    return true;
}

void CaptureWriter::closeVideo() {
    Job job;
    job.kind = Job::Kind::CloseVideo;
    push(std::move(job));
}

CaptureWriter::Pixels CaptureWriter::acquireBuffer(const std::size_t bytes) {
    Pixels pixels;
    {
        const std::lock_guard lock(mutex_);
        if (!freeBuffers_.empty()) {
            pixels = std::move(freeBuffers_.back());
            freeBuffers_.pop_back();
        }
    }
    pixels.resize(bytes);
    return pixels;
}

double CaptureWriter::pushVideoFrame(Pixels pixels) {
    Job job;
    job.kind = Job::Kind::VideoFrame;
    job.pixels = std::move(pixels);
    return push(std::move(job));
}

double CaptureWriter::pushScreenshot(Pixels pixels, const std::uint32_t width,
                                     const std::uint32_t height, const RawPixelFormat format,
                                     std::filesystem::path output) {
    Job job;
    job.kind = Job::Kind::Screenshot;
    job.pixels = std::move(pixels);
    job.width = width;
    job.height = height;
    job.format = format;
    job.output = std::move(output);
    return push(std::move(job));
}

double CaptureWriter::push(Job job) {
    const bool carriesFrame =
        job.kind == Job::Kind::VideoFrame || job.kind == Job::Kind::Screenshot;
    double waitedMs = 0.0;
    {
        std::unique_lock lock(mutex_);
        if (carriesFrame && queuedFrames_ >= capacity_) {
            const auto started = Clock::now();
            spaceAvailable_.wait(lock, [this] { return queuedFrames_ < capacity_ || stopping_; });
            waitedMs = millisecondsSince(started);
        }
        if (carriesFrame) {
            ++queuedFrames_;
            status_.queueDepth = queuedFrames_;
        }
        queue_.push_back(std::move(job));
    }
    workAvailable_.notify_one();
    return waitedMs;
}

void CaptureWriter::run() {
    for (;;) {
        Job job;
        {
            std::unique_lock lock(mutex_);
            workAvailable_.wait(lock, [this] { return !queue_.empty() || stopping_; });
            if (queue_.empty()) {
                break;
            }
            job = std::move(queue_.front());
            queue_.pop_front();
        }
        const auto started = Clock::now();
        process(job);
        writeNanoseconds_.fetch_add(
            std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now() - started).count());
        if (job.kind == Job::Kind::VideoFrame || job.kind == Job::Kind::Screenshot) {
            {
                const std::lock_guard lock(mutex_);
                --queuedFrames_;
                status_.queueDepth = queuedFrames_;
            }
            spaceAvailable_.notify_one();
            recycle(std::move(job.pixels));
        }
    }
    if (pipe_ != nullptr) {
        Job close;
        close.kind = Job::Kind::CloseVideo;
        process(close);
    }
}

void CaptureWriter::process(Job& job) {
    switch (job.kind) {
    case Job::Kind::OpenVideo:
        pipe_ = job.pipe;
        videoPath_ = job.output;
        videoFrames_ = 0;
        videoFailed_ = false;
        break;
    case Job::Kind::VideoFrame:
        if (pipe_ == nullptr || videoFailed_) {
            break;
        }
        if (std::fwrite(job.pixels.data(), 1, job.pixels.size(), pipe_) != job.pixels.size()) {
            videoFailed_ = true;
            fail("ffmpeg stopped accepting frames after " + std::to_string(videoFrames_) +
                 " frames (see its output above)");
            break;
        }
        ++videoFrames_;
        {
            const std::lock_guard lock(mutex_);
            status_.framesWritten = videoFrames_;
        }
        break;
    case Job::Kind::CloseVideo: {
        if (pipe_ == nullptr) {
            break;
        }
        const std::string problem = describeCloseStatus(VKEXP_PCLOSE(pipe_));
        pipe_ = nullptr;
        if (!problem.empty()) {
            if (!videoFailed_) {
                fail(problem + "; " + videoPath_.string() + " may be incomplete");
            }
        } else if (!videoFailed_) {
            std::cout << "[capture] Saved " << videoPath_.string() << " (" << videoFrames_
                      << " frames)\n";
            const std::lock_guard lock(mutex_);
            status_.lastFile = videoPath_;
        }
        break;
    }
    case Job::Kind::Screenshot: {
        // PNG wants RGBA; the scene alpha is not meaningful in a screenshot.
        const bool swap = job.format == RawPixelFormat::Bgra;
        for (std::size_t offset = 0; offset + 3 < job.pixels.size(); offset += 4) {
            if (swap) {
                std::swap(job.pixels[offset], job.pixels[offset + 2]);
            }
            job.pixels[offset + 3] = 255;
        }
        const std::string path = job.output.string();
        const int stride = static_cast<int>(job.width) * 4;
        if (stbi_write_png(path.c_str(), static_cast<int>(job.width),
                           static_cast<int>(job.height), 4, job.pixels.data(), stride) == 0) {
            const std::lock_guard lock(mutex_);
            status_.error = "Unable to write " + path;
            std::cerr << "[capture] " << status_.error << '\n';
            break;
        }
        std::cout << "[capture] Saved " << path << '\n';
        const std::lock_guard lock(mutex_);
        status_.lastFile = job.output;
        break;
    }
    }
}

void CaptureWriter::fail(std::string message) {
    std::cerr << "[capture] " << message << '\n';
    const std::lock_guard lock(mutex_);
    status_.failed = true;
    status_.error = std::move(message);
}

void CaptureWriter::recycle(Pixels pixels) {
    const std::lock_guard lock(mutex_);
    if (freeBuffers_.size() < capacity_ + 2) {
        freeBuffers_.push_back(std::move(pixels));
    }
}

double CaptureWriter::takeWriteMilliseconds() {
    return static_cast<double>(writeNanoseconds_.exchange(0)) / 1.0e6;
}

CaptureWriter::Status CaptureWriter::status() const {
    const std::lock_guard lock(mutex_);
    return status_;
}

void CaptureWriter::shutdown() {
    {
        const std::lock_guard lock(mutex_);
        stopping_ = true;
    }
    workAvailable_.notify_all();
    spaceAvailable_.notify_all();
    if (thread_.joinable()) {
        thread_.join();
    }
}

} // namespace vkexp
