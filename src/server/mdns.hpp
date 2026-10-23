// DNS-SD over mDNS (the CMake option TETA_WOT_MDNS): a port of
// teta-wot-server's `mdns.rs`, on mjansson/mdns (vendored in third_party/).
//
// The server advertises itself as a Thing Description Directory, as W3C WoT
// Discovery describes: a `_wot._tcp` service with the `_directory` subtype,
// whose TXT record gives the path of its TD (`td`, the well-known URL), its
// `type` (`Directory`) and its `scheme` (`http`). A client browses for
// `_directory._sub._wot._tcp.local.`, reads `/.well-known/wot`, and finds the
// Things in the directory.

#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <thread>

namespace teta::wot::detail {

/// A registered advertisement: a responder thread answering mDNS questions
/// about it, withdrawn by `stop` (or when destroyed).
class MdnsAdvertisement {
  public:
    /// Advertises the directory at `port`, as `instance` on the host
    /// `{instance}.local.` (letters and digits, every other character as
    /// `-`), with every address of the computer. Throws std::runtime_error.
    static std::unique_ptr<MdnsAdvertisement> start(const std::string& instance, std::uint16_t port);

    MdnsAdvertisement(const MdnsAdvertisement&) = delete;
    MdnsAdvertisement& operator=(const MdnsAdvertisement&) = delete;
    ~MdnsAdvertisement();

    /// Withdraws the advertisement (a DNS-SD goodbye) and stops answering.
    void stop();

    struct State;

  private:
    explicit MdnsAdvertisement(std::unique_ptr<State> state);

    std::unique_ptr<State> state_;
    std::jthread thread_;
};

}  // namespace teta::wot::detail
