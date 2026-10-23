// Blobs and MJPEG streams over HTTP: a port of teta-wot-http's `output.rs`.
//
// - `GET {prefix}/blob/{id}` serves a Blob's data, from memory or from its
//   file, with its media type (Starlette adds `charset=utf-8` to `text/*`
//   types, and so does this).
// - An invocation's `/output` serves the data itself when the output is one
//   Blob, and redirects to a remote Blob's URL.
// - `GET {prefix}/{thing}/{stream}` is an MJPEG response, and `.../viewer`
//   its page. A slow client gets the latest frame when it is ready for one.

#pragma once

#include <memory>
#include <string>
#include <string_view>

#include "teta/wot/blob.hpp"
#include "teta/wot/broker.hpp"
#include "teta/wot/http/message.hpp"
#include "teta/wot/stream.hpp"

namespace teta::wot::http::detail {

/// The content type of an MJPEG response.
inline constexpr std::string_view mjpeg_content_type = "multipart/x-mixed-replace; boundary=frame";

/// A `content-type` as Starlette writes it: `text/*` types get
/// `charset=utf-8` unless they have a charset.
std::string blob_content_type(std::string_view media_type);

/// A Blob's data as an answer: its bytes, its file's, or a 307 to a remote
/// Blob's URL.
Response blob_response(const std::shared_ptr<blob::BlobData>& data);

/// An MJPEG answer: each frame, as it comes, after `--frame` and its content
/// type. It ends when the stream stops or the broker closes (at shutdown); a
/// slow client skips frames.
Response mjpeg_response(const MjpegStream& stream, const std::shared_ptr<MessageBroker>& broker);

/// The viewer page: an `<img>` of the stream at `stream_path`.
Response viewer_page(const std::string& stream_path);

}  // namespace teta::wot::http::detail
