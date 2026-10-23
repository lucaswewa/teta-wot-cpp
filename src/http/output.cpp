#include "output.hpp"

#include <algorithm>
#include <cctype>
#include <mutex>

#include "render.hpp"
#include "teta/wot/log.hpp"

namespace teta::wot::http::detail {

namespace {

/// What precedes each frame of an MJPEG response.
constexpr std::string_view frame_header = "--frame\r\nContent-Type: image/jpeg\r\n\r\n";

// One client's MJPEG stream. It keeps itself alive from its start to its end
// (the stream stopping, the broker closing, or a send failing).
class MjpegFeed : public std::enable_shared_from_this<MjpegFeed> {
  public:
    explicit MjpegFeed(FrameReceiver frames) : frames_(std::move(frames)) {}

    void start(std::shared_ptr<StreamSink> sink, const std::shared_ptr<MessageBroker>& broker) {
        sink->set_resume([weak = weak_from_this()] {
            if (auto self = weak.lock()) {
                self->pump();
            }
        });
        {
            std::lock_guard lock(mutex_);
            sink_ = std::move(sink);
            self_ = shared_from_this();
        }
        frames_.set_notifier([weak = weak_from_this()] {
            if (auto self = weak.lock()) {
                self->pump();
            }
        });
        broker->on_close([weak = weak_from_this()] {
            if (auto self = weak.lock()) {
                self->end();
            }
        });
        pump();  // a stream already stopped
    }

  private:
    // Sends the latest frame when the client is ready for one.
    void pump() {
        std::unique_lock lock(mutex_);
        if (!sink_) {
            return;
        }
        if (frames_.stopped()) {
            end(lock);
            return;
        }
        while (sink_->ready()) {
            const auto frame = frames_.try_next();
            if (!frame) {
                return;
            }
            std::string chunk;
            chunk.reserve(frame_header.size() + frame->data->size() + 2);
            chunk.append(frame_header).append(*frame->data).append("\r\n");
            if (!sink_->send(chunk)) {
                end(lock);
                return;
            }
        }
    }

    void end() {
        std::unique_lock lock(mutex_);
        end(lock);
    }

    void end(std::unique_lock<std::mutex>& lock) {
        auto sink = std::move(sink_);
        sink_.reset();
        const auto self = std::move(self_);
        self_.reset();
        lock.unlock();
        if (sink) {
            sink->close();
        }
    }

    FrameReceiver frames_;
    std::mutex mutex_;
    std::shared_ptr<StreamSink> sink_;
    std::shared_ptr<MjpegFeed> self_;
};

}  // namespace

std::string blob_content_type(std::string_view media_type) {
    std::string lower(media_type);
    std::ranges::transform(lower, lower.begin(),
                           [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    if (media_type.starts_with("text/") && lower.find("charset=") == std::string::npos) {
        return std::string(media_type) + "; charset=utf-8";
    }
    return std::string(media_type);
}

Response blob_response(const std::shared_ptr<blob::BlobData>& data) {
    return std::visit(
        [&](const auto& content) -> Response {
            using C = std::decay_t<decltype(content)>;
            if constexpr (std::is_same_v<C, blob::BlobData::Remote>) {
                return redirect(content.href);
            } else if constexpr (std::is_same_v<C, blob::BlobData::Bytes>) {
                return Response::bytes(*content, blob_content_type(data->media_type()));
            } else {
                try {
                    return Response::bytes(blob::detail::read_file(content.path),
                                           blob_content_type(data->media_type()));
                } catch (const std::exception& error) {
                    log::error("can't read the file of Blob {}: {}", data->id().to_string(), error.what());
                    return json_response(500, json::Value{{"detail", "Internal Server Error"}});
                }
            }
        },
        data->content());
}

Response mjpeg_response(const MjpegStream& stream, const std::shared_ptr<MessageBroker>& broker) {
    auto feed = std::make_shared<MjpegFeed>(stream.frames());
    Response response;
    response.status = 200;
    response.headers.set("content-type", std::string(mjpeg_content_type));
    response.stream = [feed, broker](std::shared_ptr<StreamSink> sink) {
        feed->start(std::move(sink), broker);
    };
    return response;
}

Response viewer_page(const std::string& stream_path) {
    return Response::bytes("<html><body><img src='" + stream_path + "'></body></html>",
                           "text/html; charset=utf-8");
}

}  // namespace teta::wot::http::detail
