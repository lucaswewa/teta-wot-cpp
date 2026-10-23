#include "teta/wot/blob.hpp"

#include <fstream>
#include <iterator>
#include <mutex>
#include <optional>
#include <regex>
#include <stdexcept>
#include <system_error>
#include <unordered_map>

namespace teta::wot::blob {

namespace {

std::string trim(std::string_view text) {
    constexpr std::string_view space = " \t\r\n";
    const auto first = text.find_first_not_of(space);
    if (first == std::string_view::npos) {
        return {};
    }
    return std::string(text.substr(first, text.find_last_not_of(space) - first + 1));
}

// The registry of local data, by ID.
struct Registry {
    std::mutex mutex;
    std::unordered_map<Uuid, std::weak_ptr<BlobData>> data;
};

// Never destroyed: Blobs held by objects that outlive `main` (a server's
// runtime, which the transport keeps until the process exits) still
// unregister from it.
Registry& registry() {
    static Registry& instance = *new Registry;
    return instance;
}

thread_local Capture* current_capture = nullptr;

// A UUID as the `uuid` crate's `parse_str` reads the forms a Blob's `href`
// can have: hyphenated, or 32 hexadecimal digits.
std::optional<Uuid> parse_id(std::string_view text) {
    if (text.size() == 32 && text.find_first_not_of("0123456789abcdefABCDEF") == std::string_view::npos) {
        std::string hyphenated(text);
        for (const std::size_t at : {20, 16, 12, 8}) {
            hyphenated.insert(at, 1, '-');
        }
        return Uuid::parse(hyphenated);
    }
    return Uuid::parse(text);
}

}  // namespace

std::pair<std::string, std::string> parse_media_type(std::string_view media_type) {
    const auto semicolon = media_type.find(';');
    const std::string bare = trim(trim(media_type).substr(0, semicolon));
    auto invalid = [&](std::string_view problem) {
        return MediaTypeError(MediaTypeError::Kind::invalid,
                              "Invalid media type: " + bare + " " + std::string(problem));
    };
    const auto slash = bare.find('/');
    if (slash == std::string::npos || bare.find('/', slash + 1) != std::string::npos) {
        throw invalid("must contain exactly one '/'.");
    }
    std::string main = trim(std::string_view(bare).substr(0, slash));
    std::string sub = trim(std::string_view(bare).substr(slash + 1));
    if (main.empty() || sub.empty()) {
        throw invalid("must have both type and subtype.");
    }
    if (main == "*" && sub != "*") {
        throw invalid("has no type but has a subtype.");
    }
    return {std::move(main), std::move(sub)};
}

bool media_types_match(std::string_view media_type, std::string_view pattern) {
    const auto [main, sub] = parse_media_type(media_type);
    const auto [pattern_main, pattern_sub] = parse_media_type(pattern);
    return (pattern_main == "*" || main == pattern_main) && (pattern_sub == "*" || sub == pattern_sub);
}

// ---- Temporary directories --------------------------------------------------------------

TemporaryDirectory::TemporaryDirectory()
    : path_(std::filesystem::temp_directory_path() / ("teta-wot-blob-" + Uuid::v4().to_string())) {
    std::filesystem::create_directories(path_);
}

TemporaryDirectory::TemporaryDirectory(TemporaryDirectory&& other) noexcept : path_(std::move(other.path_)) {
    other.path_.clear();
}

TemporaryDirectory& TemporaryDirectory::operator=(TemporaryDirectory&& other) noexcept {
    if (this != &other) {
        std::error_code ignored;
        if (!path_.empty()) {
            std::filesystem::remove_all(path_, ignored);
        }
        path_ = std::move(other.path_);
        other.path_.clear();
    }
    return *this;
}

TemporaryDirectory::~TemporaryDirectory() {
    if (!path_.empty()) {
        std::error_code ignored;
        std::filesystem::remove_all(path_, ignored);
    }
}

// ---- Data ---------------------------------------------------------------------------------

std::shared_ptr<BlobData> BlobData::make(std::string media_type, Content content) {
    std::shared_ptr<BlobData> data(new BlobData(Uuid::v4(), std::move(media_type), std::move(content)));
    if (!data->is_remote()) {
        auto& r = registry();
        std::lock_guard lock(r.mutex);
        r.data[data->id_] = data;
    }
    return data;
}

std::shared_ptr<BlobData> BlobData::find(const Uuid& id) {
    // The weak reference is taken out before upgrading, so that the lock
    // isn't held if the upgraded reference is the last one dropped.
    std::weak_ptr<BlobData> weak;
    {
        auto& r = registry();
        std::lock_guard lock(r.mutex);
        const auto it = r.data.find(id);
        if (it == r.data.end()) {
            return nullptr;
        }
        weak = it->second;
    }
    return weak.lock();
}

BlobData::~BlobData() {
    if (!is_remote()) {
        auto& r = registry();
        std::lock_guard lock(r.mutex);
        r.data.erase(id_);
    }
}

std::string BlobData::href() const {
    if (const auto* remote = std::get_if<Remote>(&content_)) {
        return remote->href;
    }
    return local_href(id_);
}

std::string local_href(const Uuid& id) {
    return "blob/" + id.to_string();
}

std::optional<Uuid> local_href_id(std::string_view href) {
    if (!href.starts_with("blob/")) {
        return std::nullopt;
    }
    const auto id = parse_id(href.substr(5));
    if (!id || !BlobData::find(*id)) {
        return std::nullopt;
    }
    return id;
}

// ---- Capture ------------------------------------------------------------------------------

Capture::Capture() : previous_(current_capture) {
    current_capture = this;
}

Capture::~Capture() {
    current_capture = previous_;
}

std::vector<std::shared_ptr<BlobData>> Capture::take() {
    return std::exchange(blobs_, {});
}

void Capture::record(const std::shared_ptr<BlobData>& data) {
    if (current_capture) {
        current_capture->blobs_.push_back(data);
    }
}

std::shared_ptr<BlobData> as_blob(const json::Value& value,
                                  const std::vector<std::shared_ptr<BlobData>>& blobs) {
    if (!value.is_object() || blobs.size() != 1) {
        return nullptr;
    }
    const auto href = value.find("href");
    const auto rel = value.find("rel");
    if (href == value.end() || !href->is_string() || rel == value.end() || *rel != "output" ||
        blobs.front()->href() != href->get<std::string>()) {
        return nullptr;
    }
    return blobs.front();
}

// ---- Reading ------------------------------------------------------------------------------

namespace detail {

void throw_not_found(const std::string& message) {
    throw std::runtime_error(message);
}

void throw_remote(const std::string& href) {
    throw std::runtime_error("the Blob's data is at " + href + ", not on this server");
}

std::string read_file(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        throw std::runtime_error("can't read the Blob's file");
    }
    return {std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
}

std::shared_ptr<BlobData> decode(const json::Value& value, std::string_view pattern) {
    // The JSON form (`BlobModel`), as serde derives it: a string `href` and
    // `media_type`, from an object (fields checked in its order) or an array.
    const auto string_field = [](const json::Value& field) {
        if (!field.is_string()) {
            codec::invalid_type(field, "a string");
        }
        return field.get<std::string>();
    };
    std::optional<std::string> text;
    std::optional<std::string> media_type;
    if (value.is_object()) {
        for (const auto& [key, field] : value.items()) {
            if (key == "href") {
                text = string_field(field);
            } else if (key == "media_type") {
                media_type = string_field(field);
            }
        }
        if (!text) {
            throw DecodeError("missing field `href`");
        }
        if (!media_type) {
            throw DecodeError("missing field `media_type`");
        }
    } else if (value.is_array()) {
        const auto element = [&](std::size_t index) {
            if (value.size() <= index) {
                throw DecodeError("invalid length " + std::to_string(index) +
                                  ", expected struct BlobModel with 2 elements");
            }
            return string_field(value[index]);
        };
        text = element(0);
        media_type = element(1);
        if (value.size() > 2) {
            throw DecodeError("invalid length " + std::to_string(value.size()) +
                              ", expected fewer elements in array");
        }
    } else {
        codec::invalid_type(value, "struct BlobModel");
    }
    static const std::regex pattern_of_id("blob/([0-9a-z\\-]+)");
    std::smatch match;
    if (!std::regex_search(*text, match, pattern_of_id)) {
        throw ValueDecodeError("Blob URLs must contain a Blob ID.");
    }
    const auto id = parse_id(match[1].str());
    if (!id) {
        throw ValueDecodeError("badly formed hexadecimal UUID string");
    }
    auto data = BlobData::find(*id);
    if (!data) {
        throw ValueDecodeError("Blob ID " + id->to_string() + " wasn't found on this server.");
    }
    bool matches = false;
    try {
        matches = media_types_match(data->media_type(), pattern);
    } catch (const MediaTypeError& error) {
        throw ValueDecodeError(error.what());
    }
    if (!matches) {
        throw ValueDecodeError("Blob data media_type '" + data->media_type() +
                               "' does not match Blob media_type '" + std::string(pattern) + "'.");
    }
    return data;
}

}  // namespace detail

}  // namespace teta::wot::blob
