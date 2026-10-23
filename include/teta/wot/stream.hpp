// MJPEG streams: JPEG frames pushed from any thread, and watched by any number
// of clients. A port of teta-wot-core's `stream`.
//
// A Thing holds an MjpegStream member, declared with `t.stream("stream",
// &Camera::stream)`. The HTTP binding serves it at `/{thing}/{stream}` as
// `multipart/x-mixed-replace`, which a browser's `<img>` plays, with a viewer
// page at `/{thing}/{stream}/viewer`.
//
// - `add_frame` never waits, and works from a camera's thread. It checks the
//   JPEG start and end markers.
// - The last frames (10 by default) are kept in a ring buffer, by index.
// - Waiting for a frame (`next_frame`, `grab_frame`, `next_frame_size`) waits
//   for the next one to arrive.
// - Each client gets the latest frame whenever it is ready for one; a slow
//   client skips frames, and never holds up the camera or other clients.
// - `stop` ends every client's stream; `reset` starts again.

#pragma once

#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <stop_token>
#include <string>
#include <vector>

namespace teta::wot {

/// How many frames a stream keeps by default.
inline constexpr std::size_t default_frame_buffer = 10;

/// One frame of a stream.
struct Frame {
    /// The JPEG data.
    std::shared_ptr<const std::string> data;
    /// When the frame was captured, or else when it was added.
    std::chrono::system_clock::time_point timestamp;
    /// 0 for the first frame after the stream was made or reset, then
    /// counting up.
    std::uint64_t index = 0;
};

/// A stream operation failed.
class StreamError : public std::exception {
  public:
    enum class Kind {
        /// The frame doesn't start and end with the JPEG markers.
        invalid_jpeg,
        /// The stream was stopped while waiting.
        stopped,
        /// The frame has left the ring buffer.
        overwritten,
        /// The frame hasn't been added yet.
        not_yet_acquired,
    };
    explicit StreamError(Kind kind) : kind_(kind) {}
    Kind kind() const { return kind_; }
    const char* what() const noexcept override;

  private:
    Kind kind_;
};

class FrameReceiver;

/// An MJPEG stream. A cheap handle: copies are the same stream, so a camera
/// thread can have its own.
class MjpegStream {
  public:
    /// A stream keeping the last `default_frame_buffer` frames.
    MjpegStream() : MjpegStream(default_frame_buffer) {}
    /// A stream keeping the last `size` frames (at least one).
    explicit MjpegStream(std::size_t size);

    /// Adds a JPEG frame, timestamped now, and returns its index. Never
    /// waits: call it from any thread. Throws StreamError (`invalid_jpeg`).
    std::uint64_t add_frame(std::string frame) const {
        return add_frame_at(std::move(frame), std::chrono::system_clock::now());
    }
    /// Adds a JPEG frame captured at `timestamp`.
    std::uint64_t add_frame_at(std::string frame, std::chrono::system_clock::time_point timestamp) const;

    /// The frame with this index, if it is still in the ring buffer (the
    /// oldest slot counts as overwritten, as the next frame is about to
    /// replace it). Throws StreamError.
    Frame frame(std::uint64_t index) const;
    /// The latest frame, if there is one.
    std::optional<Frame> latest_frame() const;

    /// Waits for the next frame, and returns its index; throws StreamError
    /// (`stopped`) if the stream stops (or is stopped already), or if `stop`
    /// is requested.
    std::uint64_t next_frame(std::stop_token stop = {}) const;
    /// Waits for the next frame, and returns it.
    std::string grab_frame(std::stop_token stop = {}) const;
    /// Waits for the next frame, and returns its size in bytes (a sharpness
    /// measure, for autofocus).
    std::size_t next_frame_size(std::stop_token stop = {}) const;

    /// Stops the stream: every client's stream ends, and waiters get
    /// StreamError (`stopped`).
    void stop() const;
    /// Empties the ring buffer, optionally resizes it, restarts the index at
    /// 0, and streams again.
    void reset(std::optional<std::size_t> size = std::nullopt) const;
    /// Whether the stream is streaming (not stopped).
    bool is_streaming() const;

    /// Receives the frames added from now on, as a client of the stream
    /// does. A slow receiver gets the latest frame when it asks, skipping the
    /// ones in between.
    FrameReceiver frames() const;

    struct Shared;

  private:
    friend class FrameReceiver;
    std::shared_ptr<Shared> shared_;
};

/// Receives a stream's frames: see MjpegStream::frames.
class FrameReceiver {
  public:
    FrameReceiver(FrameReceiver&&) noexcept;
    FrameReceiver& operator=(FrameReceiver&&) noexcept;
    ~FrameReceiver();

    /// The next frame, waiting for it; nullopt once the stream is stopped.
    std::optional<Frame> next();
    /// The latest frame if one was added since the last call, without
    /// waiting.
    std::optional<Frame> try_next();
    /// Whether the stream is stopped.
    bool stopped() const;
    /// Calls `notify` (from the camera's or the stopping thread, without
    /// locks held) whenever a frame is added or the stream stops: for
    /// consumers that don't wait in `next`.
    void set_notifier(std::function<void()> notify);

  private:
    friend class MjpegStream;
    struct Notifier;
    FrameReceiver(std::shared_ptr<MjpegStream::Shared> shared, std::uint64_t seen);

    std::shared_ptr<MjpegStream::Shared> shared_;
    std::uint64_t seen_;
    std::shared_ptr<Notifier> notifier_;
};

}  // namespace teta::wot
