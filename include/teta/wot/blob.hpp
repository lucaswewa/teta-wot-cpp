// Blobs: binary data that actions take and return, downloaded from
// `/blob/{id}`. A port of teta-wot-core's `blob`.
//
// A `Blob<M>` is a handle to data held in memory, in a file, in a file of a
// temporary directory, or at a remote URL. `M` is its media type: `Jpeg`,
// `TextPlain`, or one of your own:
//
//     struct Spectrum {
//         static constexpr std::string_view media_type = "text/csv";
//         static constexpr std::string_view title = "SpectrumBlob";
//         static constexpr std::string_view description = "A spectrum, one wavelength per line.";
//     };
//
// In JSON, a Blob is a link object:
//
//     {"href": "http://host/blob/<id>", "media_type": "image/jpeg", "rel": "output", "description": "..."}
//
// Lifetime. Local data is registered under a random ID in a registry of weak
// references: it can be downloaded for as long as something holds the Blob.
// An invocation holds the Blobs of its input and output until it expires, so
// a Blob output can be downloaded for the action's retention time. When the
// last handle goes, the data is freed, and a temporary directory is deleted.
//
// Serialisation. Encoded outside a request, a local Blob's `href` is the
// relative reference `blob/<id>`; the HTTP binding makes it absolute, with
// the request's host and API prefix, when it writes a response. Decoding
// reads the ID from the `href` (anything with `blob/<id>`) and finds the data
// in the registry; a Blob that isn't there is pydantic's `value_error`.

#pragma once

#include <exception>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

#include "teta/wot/codec.hpp"
#include "teta/wot/uuid.hpp"

namespace teta::wot {

/// A media type of Blobs: a type with `static constexpr std::string_view
/// media_type` (which may have wildcards, `image/*`), `title` (the schema's
/// title) and, optionally, `description` (written in the Blob's JSON form).
template <typename M>
concept BlobMediaType = requires {
    { M::media_type } -> std::convertible_to<std::string_view>;
    { M::title } -> std::convertible_to<std::string_view>;
};

namespace blob {

/// The description of a Blob's JSON form, unless its media type has one.
inline constexpr std::string_view default_description =
    "The output from this action is not serialised to JSON, so it must be retrieved as a file. This link "
    "will "
    "return the file.";

/// Any media type (`*/*`).
struct AnyMedia {
    static constexpr std::string_view media_type = "*/*";
    static constexpr std::string_view title = "Blob";
};
/// JPEG images.
struct Jpeg {
    static constexpr std::string_view media_type = "image/jpeg";
    static constexpr std::string_view title = "JPEGBlob";
};
/// PNG images.
struct Png {
    static constexpr std::string_view media_type = "image/png";
    static constexpr std::string_view title = "PNGBlob";
};
/// TIFF images.
struct Tiff {
    static constexpr std::string_view media_type = "image/tiff";
    static constexpr std::string_view title = "TIFFBlob";
};
/// Plain text.
struct TextPlain {
    static constexpr std::string_view media_type = "text/plain";
    static constexpr std::string_view title = "TextBlob";
};
/// Comma-separated values.
struct Csv {
    static constexpr std::string_view media_type = "text/csv";
    static constexpr std::string_view title = "CSVBlob";
};
/// JSON documents.
struct Json {
    static constexpr std::string_view media_type = "application/json";
    static constexpr std::string_view title = "JSONBlob";
};
/// Arbitrary bytes.
struct OctetStream {
    static constexpr std::string_view media_type = "application/octet-stream";
    static constexpr std::string_view title = "BinaryBlob";
};

/// The description a media type gives its Blobs, or the default.
template <BlobMediaType M>
constexpr std::string_view description_of() {
    if constexpr (requires { std::string_view(M::description); }) {
        return M::description;
    } else {
        return default_description;
    }
}

/// A media type is malformed, or doesn't match a Blob's.
class MediaTypeError : public std::exception {
  public:
    enum class Kind {
        /// Not `type/subtype`.
        invalid,
        /// The data's media type isn't the Blob's.
        mismatch,
    };
    MediaTypeError(Kind kind, std::string message) : kind_(kind), message_(std::move(message)) {}
    Kind kind() const { return kind_; }
    const char* what() const noexcept override { return message_.c_str(); }

  private:
    Kind kind_;
    std::string message_;
};

/// Splits a media type into its type and subtype, ignoring parameters
/// (`text/plain; charset=utf-8` → `text`, `plain`). Throws MediaTypeError.
std::pair<std::string, std::string> parse_media_type(std::string_view media_type);

/// Whether a media type matches a pattern that may have wildcards
/// (`image/png` matches `image/*` and `*/*`). Throws MediaTypeError.
bool media_types_match(std::string_view media_type, std::string_view pattern);

/// A temporary directory, deleted with the Blob made from a file in it.
class TemporaryDirectory {
  public:
    /// Makes a new, empty directory. Throws std::filesystem::filesystem_error.
    TemporaryDirectory();
    TemporaryDirectory(TemporaryDirectory&& other) noexcept;
    TemporaryDirectory& operator=(TemporaryDirectory&& other) noexcept;
    TemporaryDirectory(const TemporaryDirectory&) = delete;
    TemporaryDirectory& operator=(const TemporaryDirectory&) = delete;
    ~TemporaryDirectory();
    const std::filesystem::path& path() const { return path_; }

  private:
    std::filesystem::path path_;
};

/// A Blob's data: registered under its ID while anything holds it, unless it
/// is remote.
class BlobData {
  public:
    /// Bytes in memory.
    using Bytes = std::shared_ptr<const std::string>;
    /// A file on disk, and the temporary directory deleted with the data.
    struct File {
        std::filesystem::path path;
        std::shared_ptr<TemporaryDirectory> folder;
    };
    /// Data at a URL, not on this server.
    struct Remote {
        std::string href;
    };
    using Content = std::variant<Bytes, File, Remote>;

    /// Registers new data (unless it is remote) under a random ID.
    static std::shared_ptr<BlobData> make(std::string media_type, Content content);
    /// The data registered under `id`, if anything still holds it.
    static std::shared_ptr<BlobData> find(const Uuid& id);

    BlobData(const BlobData&) = delete;
    BlobData& operator=(const BlobData&) = delete;
    ~BlobData();

    /// The ID it is downloaded by (a remote Blob has one too, unused).
    const Uuid& id() const { return id_; }
    const std::string& media_type() const { return media_type_; }
    const Content& content() const { return content_; }
    bool is_remote() const { return std::holds_alternative<Remote>(content_); }
    /// The `href` written when the Blob is encoded: the remote URL, or
    /// `blob/<id>`, which the HTTP binding makes absolute.
    std::string href() const;

  private:
    BlobData(Uuid id, std::string media_type, Content content)
        : id_(id), media_type_(std::move(media_type)), content_(std::move(content)) {}

    Uuid id_;
    std::string media_type_;
    Content content_;
};

/// The relative `href` of local data: `blob/<id>`.
std::string local_href(const Uuid& id);
/// The ID in an `href` written by `local_href`, if `href` is exactly that and
/// the data still exists.
std::optional<Uuid> local_href_id(std::string_view href);

/// Collects the data of the Blobs encoded on this thread while it exists:
/// what an invocation keeps alive with its input and output. Nested captures
/// collect into the innermost.
class [[nodiscard]] Capture {
  public:
    Capture();
    Capture(const Capture&) = delete;
    Capture& operator=(const Capture&) = delete;
    ~Capture();
    /// The data of the Blobs encoded so far.
    std::vector<std::shared_ptr<BlobData>> take();

    /// Records `data` in the current capture, if there is one.
    static void record(const std::shared_ptr<BlobData>& data);

  private:
    std::vector<std::shared_ptr<BlobData>> blobs_;
    Capture* previous_;
};

/// The Blob's data, if `value` is exactly one Blob's JSON form and `blobs`
/// holds its data.
std::shared_ptr<BlobData> as_blob(const json::Value& value,
                                  const std::vector<std::shared_ptr<BlobData>>& blobs);

namespace detail {
[[noreturn]] void throw_not_found(const std::string& message);
[[noreturn]] void throw_remote(const std::string& href);
std::string read_file(const std::filesystem::path& path);
}  // namespace detail

}  // namespace blob

/// Binary data that an action takes or returns, of the media type `M`. A
/// cheap handle: copies share the data, which is freed with the last one.
template <BlobMediaType M = blob::AnyMedia>
class Blob {
  public:
    /// No data: what a struct holding a Blob is before it is read. It can't
    /// be encoded.
    Blob() = default;

    /// Data in memory, of `M`'s media type.
    static Blob from_bytes(std::string data) {
        return Blob(blob::BlobData::make(std::string(M::media_type),
                                         std::make_shared<const std::string>(std::move(data))));
    }
    /// Data in memory, of a more specific media type than `M`'s (such as
    /// `text/csv` for a `text/*` Blob). Throws blob::MediaTypeError.
    static Blob from_bytes_as(std::string data, std::string_view media_type) {
        return Blob(blob::BlobData::make(media_type_for(media_type),
                                         std::make_shared<const std::string>(std::move(data))));
    }
    /// A file that stays on disk for at least as long as the Blob. Throws
    /// std::runtime_error if it doesn't exist.
    static Blob from_file(std::filesystem::path path) {
        return file(std::move(path), nullptr, M::media_type);
    }
    /// A file of a more specific media type than `M`'s.
    static Blob from_file_as(std::filesystem::path path, std::string_view media_type) {
        return file(std::move(path), nullptr, media_type);
    }
    /// A file in a temporary directory, which is deleted with the Blob's
    /// data: the safe way to return a file made for the purpose.
    static Blob from_temporary_directory(blob::TemporaryDirectory folder, const std::filesystem::path& name) {
        auto path = folder.path() / name;
        return file(std::move(path), std::make_shared<blob::TemporaryDirectory>(std::move(folder)),
                    M::media_type);
    }
    /// Data at a URL, not on this server: its JSON form links to the URL,
    /// and the invocation's `/output` redirects there.
    static Blob from_url(std::string href) {
        return Blob(
            blob::BlobData::make(std::string(M::media_type), blob::BlobData::Remote{std::move(href)}));
    }

    /// Sets the description written in the Blob's JSON form.
    Blob with_description(std::string description) && {
        description_ = std::move(description);
        return std::move(*this);
    }

    /// The media type of the data: `M`'s, or a more specific one.
    const std::string& media_type() const { return data_->media_type(); }
    /// The ID the data is downloaded by, unless it is remote.
    std::optional<Uuid> id() const {
        if (!data_ || data_->is_remote()) {
            return std::nullopt;
        }
        return data_->id();
    }
    /// The description written in the Blob's JSON form.
    std::string description() const { return description_.value_or(std::string(blob::description_of<M>())); }
    /// The data's registry entry, as the HTTP binding serves it.
    const std::shared_ptr<blob::BlobData>& data() const { return data_; }

    /// Reads the data into memory. Remote data can't be read (it throws).
    std::string bytes() const {
        return std::visit(
            [](const auto& content) -> std::string {
                using C = std::decay_t<decltype(content)>;
                if constexpr (std::is_same_v<C, blob::BlobData::Bytes>) {
                    return *content;
                } else if constexpr (std::is_same_v<C, blob::BlobData::File>) {
                    return blob::detail::read_file(content.path);
                } else {
                    blob::detail::throw_remote(content.href);
                }
            },
            data_->content());
    }

    /// The same data as a Blob of another media type, if the data's type
    /// matches it. Throws blob::MediaTypeError.
    template <BlobMediaType N>
    Blob<N> cast() const {
        (void)Blob<N>::media_type_for(data_->media_type());
        Blob<N> out(data_);
        out.description_ = description_;
        return out;
    }

  private:
    template <BlobMediaType>
    friend class Blob;
    friend struct Codec<Blob>;

    explicit Blob(std::shared_ptr<blob::BlobData> data) : data_(std::move(data)) {}

    // A media type checked against `M`'s.
    static std::string media_type_for(std::string_view media_type) {
        if (!blob::media_types_match(media_type, M::media_type)) {
            throw blob::MediaTypeError(blob::MediaTypeError::Kind::mismatch,
                                       "Can't create a " + std::string(M::title) + " as media type '" +
                                           std::string(media_type) + "' doesn't match '" +
                                           std::string(M::media_type) + "'.");
        }
        return std::string(media_type);
    }

    static Blob file(std::filesystem::path path, std::shared_ptr<blob::TemporaryDirectory> folder,
                     std::string_view media_type) {
        auto checked = media_type_for(media_type);
        std::error_code error;
        if (!std::filesystem::is_regular_file(path, error)) {
            blob::detail::throw_not_found("Tried to return a file that doesn't exist.");
        }
        return Blob(blob::BlobData::make(std::move(checked),
                                         blob::BlobData::File{std::move(path), std::move(folder)}));
    }

    std::shared_ptr<blob::BlobData> data_;
    std::optional<std::string> description_;
};

namespace blob::detail {

/// The description of a Blob's JSON form in schemas.
inline constexpr std::string_view model_description =
    "A model for JSON-serialised `.Blob` objects.\n\nThis model describes the JSON representation of a "
    "`.Blob`\nand "
    "is used to describe the `.Blob` object in JSON responses.\n\nThe binary data may be retrieved with a "
    "``GET`` "
    "request to the URL\nspecified in ``href`` which should return it directly in the body of\nthe response.";

/// The data a Blob's JSON form refers to, checked against `pattern`; throws
/// ValueDecodeError with teta-wot's messages.
std::shared_ptr<BlobData> decode(const json::Value& value, std::string_view pattern);

}  // namespace blob::detail

template <BlobMediaType M>
struct Codec<Blob<M>> {
    static constexpr bool referenced = false;

    static json::Value json_schema() {
        json::Value media_type = json::Value::object();
        media_type["title"] = "Media Type";
        if (M::media_type.find('*') == std::string_view::npos) {
            media_type["const"] = std::string(M::media_type);
        }
        media_type["default"] = std::string(M::media_type);
        media_type["type"] = "string";
        json::Value description = {{"title", "Description"},
                                   {"default", std::string(blob::description_of<M>())},
                                   {"type", "string"}};
        json::Value schema = json::Value::object();
        schema["description"] = std::string(blob::detail::model_description);
        schema["title"] = std::string(M::title);
        schema["type"] = "object";
        schema["properties"] = {
            {"href", {{"title", "Href"}, {"type", "string"}}},
            {"media_type", std::move(media_type)},
            {"rel", {{"title", "Rel"}, {"const", "output"}, {"default", "output"}, {"type", "string"}}},
            {"description", std::move(description)}};
        schema["required"] = json::Value::array({"href", "media_type"});
        return schema;
    }

    static json::Value encode(const Blob<M>& blob) {
        if (!blob.data_) {
            throw std::invalid_argument("a Blob without data can't be written as JSON");
        }
        blob::Capture::record(blob.data_);
        json::Value out = json::Value::object();
        out["href"] = blob.data_->href();
        out["media_type"] = blob.data_->media_type();
        out["rel"] = "output";
        out["description"] = blob.description();
        return out;
    }

    static Blob<M> decode(const json::Value& value) {
        return Blob<M>(blob::detail::decode(value, M::media_type));
    }
};

}  // namespace teta::wot
