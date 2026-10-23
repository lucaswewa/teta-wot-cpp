#include "teta/wot/stream.hpp"

#include <algorithm>

namespace teta::wot {

struct FrameReceiver::Notifier {
    std::function<void()> notify;
};

struct MjpegStream::Shared {
    mutable std::mutex mutex;
    std::condition_variable_any changed;
    std::vector<std::optional<Frame>> frames;
    std::optional<std::uint64_t> last;
    bool streaming = true;
    /// Counts every change of `last` and `streaming`.
    std::uint64_t version = 0;
    std::vector<std::weak_ptr<FrameReceiver::Notifier>> notifiers;

    // Wakes waiters and notifies receivers; called with the lock held, which
    // it releases before calling the notifiers.
    void changed_state(std::unique_lock<std::mutex>& lock) {
        ++version;
        std::vector<std::shared_ptr<FrameReceiver::Notifier>> live;
        std::erase_if(notifiers, [&](const auto& weak) {
            auto notifier = weak.lock();
            if (!notifier) {
                return true;
            }
            live.push_back(std::move(notifier));
            return false;
        });
        lock.unlock();
        changed.notify_all();
        for (const auto& notifier : live) {
            if (notifier->notify) {
                notifier->notify();
            }
        }
    }
};

const char* StreamError::what() const noexcept {
    switch (kind_) {
        case Kind::invalid_jpeg:
            return "Invalid JPEG";
        case Kind::stopped:
            return "the stream has stopped";
        case Kind::overwritten:
            return "the ith frame has been overwritten";
        case Kind::not_yet_acquired:
            break;
    }
    return "the ith frame has not yet been acquired";
}

MjpegStream::MjpegStream(std::size_t size) : shared_(std::make_shared<Shared>()) {
    shared_->frames.resize(std::max<std::size_t>(size, 1));
}

std::uint64_t MjpegStream::add_frame_at(std::string frame,
                                        std::chrono::system_clock::time_point timestamp) const {
    const auto n = frame.size();
    const auto byte = [&](std::size_t i) { return static_cast<unsigned char>(frame[i]); };
    if (n < 2 || byte(0) != 0xff || byte(1) != 0xd8 || byte(n - 2) != 0xff || byte(n - 1) != 0xd9) {
        throw StreamError(StreamError::Kind::invalid_jpeg);
    }
    std::unique_lock lock(shared_->mutex);
    const std::uint64_t index = shared_->last ? *shared_->last + 1 : 0;
    shared_->frames[index % shared_->frames.size()] =
        Frame{std::make_shared<const std::string>(std::move(frame)), timestamp, index};
    shared_->last = index;
    shared_->changed_state(lock);
    return index;
}

Frame MjpegStream::frame(std::uint64_t index) const {
    std::lock_guard lock(shared_->mutex);
    if (!shared_->last || index > *shared_->last) {
        throw StreamError(StreamError::Kind::not_yet_acquired);
    }
    const std::uint64_t size = shared_->frames.size();
    if (index + size < *shared_->last + 2) {
        throw StreamError(StreamError::Kind::overwritten);
    }
    const auto& slot = shared_->frames[index % size];
    if (!slot || slot->index != index) {
        throw StreamError(StreamError::Kind::overwritten);
    }
    return *slot;
}

std::optional<Frame> MjpegStream::latest_frame() const {
    std::lock_guard lock(shared_->mutex);
    if (!shared_->last) {
        return std::nullopt;
    }
    return shared_->frames[*shared_->last % shared_->frames.size()];
}

std::uint64_t MjpegStream::next_frame(std::stop_token stop) const {
    std::unique_lock lock(shared_->mutex);
    if (!shared_->streaming) {
        throw StreamError(StreamError::Kind::stopped);
    }
    std::uint64_t seen = shared_->version;
    for (;;) {
        if (!shared_->changed.wait(lock, stop, [&] { return shared_->version != seen; })) {
            throw StreamError(StreamError::Kind::stopped);
        }
        seen = shared_->version;
        if (!shared_->streaming) {
            throw StreamError(StreamError::Kind::stopped);
        }
        if (shared_->last) {
            return *shared_->last;
        }
    }
}

std::string MjpegStream::grab_frame(std::stop_token stop) const {
    return *frame(next_frame(std::move(stop))).data;
}

std::size_t MjpegStream::next_frame_size(std::stop_token stop) const {
    return frame(next_frame(std::move(stop))).data->size();
}

void MjpegStream::stop() const {
    std::unique_lock lock(shared_->mutex);
    shared_->streaming = false;
    shared_->changed_state(lock);
}

void MjpegStream::reset(std::optional<std::size_t> size) const {
    std::unique_lock lock(shared_->mutex);
    const auto slots = std::max<std::size_t>(size.value_or(shared_->frames.size()), 1);
    shared_->frames.assign(slots, std::nullopt);
    shared_->last.reset();
    shared_->streaming = true;
    shared_->changed_state(lock);
}

bool MjpegStream::is_streaming() const {
    std::lock_guard lock(shared_->mutex);
    return shared_->streaming;
}

FrameReceiver MjpegStream::frames() const {
    std::lock_guard lock(shared_->mutex);
    return FrameReceiver(shared_, shared_->version);
}

// ---- Receivers ----------------------------------------------------------------------------

FrameReceiver::FrameReceiver(std::shared_ptr<MjpegStream::Shared> shared, std::uint64_t seen)
    : shared_(std::move(shared)), seen_(seen) {}

FrameReceiver::FrameReceiver(FrameReceiver&&) noexcept = default;
FrameReceiver& FrameReceiver::operator=(FrameReceiver&&) noexcept = default;
FrameReceiver::~FrameReceiver() = default;

std::optional<Frame> FrameReceiver::next() {
    std::unique_lock lock(shared_->mutex);
    if (!shared_->streaming) {
        return std::nullopt;
    }
    for (;;) {
        shared_->changed.wait(lock, [&] { return shared_->version != seen_; });
        seen_ = shared_->version;
        if (!shared_->streaming) {
            return std::nullopt;
        }
        if (shared_->last) {
            return shared_->frames[*shared_->last % shared_->frames.size()];
        }
    }
}

std::optional<Frame> FrameReceiver::try_next() {
    std::lock_guard lock(shared_->mutex);
    if (shared_->version == seen_) {
        return std::nullopt;
    }
    seen_ = shared_->version;
    if (!shared_->streaming || !shared_->last) {
        return std::nullopt;
    }
    return shared_->frames[*shared_->last % shared_->frames.size()];
}

bool FrameReceiver::stopped() const {
    std::lock_guard lock(shared_->mutex);
    return !shared_->streaming;
}

void FrameReceiver::set_notifier(std::function<void()> notify) {
    auto notifier = std::make_shared<Notifier>();
    notifier->notify = std::move(notify);
    {
        std::lock_guard lock(shared_->mutex);
        shared_->notifiers.push_back(notifier);
    }
    notifier_ = std::move(notifier);
}

}  // namespace teta::wot
