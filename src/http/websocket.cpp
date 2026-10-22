#include "websocket.hpp"

#include <mutex>

#include "teta/wot/log.hpp"

namespace teta::wot::http::detail {

namespace {

std::optional<Target> target_of(std::string_view message_type) {
    if (message_type == "addPropertyObservation") {
        return Target::property;
    }
    if (message_type == "addActionObservation") {
        return Target::action;
    }
    if (message_type == "addEventSubscription") {
        return Target::event;
    }
    return std::nullopt;
}

std::string_view noun(Target target) {
    switch (target) {
        case Target::property:
            return "property";
        case Target::action:
            return "action";
        case Target::event:
            break;
    }
    return "event";
}

// The `operation` of an error response.
std::string_view operation(Target target) {
    switch (target) {
        case Target::property:
            return "observeproperty";
        case Target::action:
            return "observeaction";
        case Target::event:
            break;
    }
    return "subscribeevent";
}

// As Rust's `{:?}` writes the reason.
std::string_view debug(Refusal refusal) {
    return refusal == Refusal::not_found ? "NotFound" : "NotObservable";
}

class ThingSocket final : public WebSocketSession, public std::enable_shared_from_this<ThingSocket> {
  public:
    ThingSocket(std::shared_ptr<MessageBroker> broker, std::shared_ptr<const ThingHandle> thing,
                std::shared_ptr<WebSocketPeer> peer)
        : broker_(std::move(broker)),
          thing_(std::move(thing)),
          peer_(std::move(peer)),
          subscription_(broker_->subscription()) {}

    void start() {
        subscription_.set_notifier([weak = weak_from_this()] {
            if (auto self = weak.lock()) {
                self->pump();
            }
        });
        peer_->set_resume([weak = weak_from_this()] {
            if (auto self = weak.lock()) {
                self->pump();
            }
        });
        pump();  // a broker already closed
    }

    void on_text(std::string_view text) override {
        if (auto reply = handle(text)) {
            std::lock_guard lock(mutex_);
            if (!gone_ && !peer_->send_text(json::dump(*reply))) {
                gone_ = true;
            }
        }
    }

    void on_binary() override {
        log::error("Got a bad websocket message: a binary message, where JSON text was expected.");
    }

    void on_closed() override {
        std::lock_guard lock(mutex_);
        gone_ = true;
    }

  private:
    // Relays what is buffered while the client keeps up; closes the socket
    // once the broker is closed and everything buffered is sent.
    void pump() {
        std::lock_guard lock(mutex_);
        if (gone_) {
            return;
        }
        while (peer_->ready()) {
            auto message = subscription_.try_recv();
            if (!message) {
                break;
            }
            if (!peer_->send_text(json::dump(relay(*message)))) {
                gone_ = true;
                return;
            }
        }
        if (subscription_.finished() && !closing_) {
            closing_ = true;
            peer_->close(going_away, "the server is shutting down");
        }
    }

    // Subscribes to what a client message names; returns the error response
    // to send, if any.
    std::optional<json::Value> handle(std::string_view text) {
        auto bad = [&](std::string_view why) {
            log::error("Got a bad websocket message: {}, caused {}.", text, why);
        };
        json::Value request;
        try {
            request = json::parse(text);
        } catch (const json::ParseError& error) {
            bad(std::string("a JSON error: ") + error.what());
            return std::nullopt;
        }
        const auto message_type = request.is_object() ? request.find("messageType") : request.end();
        if (!request.is_object() || message_type == request.end() || !message_type->is_string()) {
            bad("a missing messageType");
            return std::nullopt;
        }
        const auto target = target_of(message_type->get<std::string>());
        if (!target) {
            return std::nullopt;
        }
        const auto data = request.find("data");
        if (data == request.end() || !data->is_object()) {
            bad("data that isn't an object");
            return std::nullopt;
        }
        for (const auto& [name, _] : data->items()) {
            std::optional<Refusal> refusal;
            switch (*target) {
                case Target::property:
                    if (const auto* property = thing_->property(name); !property) {
                        refusal = Refusal::not_found;
                    } else if (!property->is_observable()) {
                        refusal = Refusal::not_observable;
                    }
                    break;
                case Target::action:
                    if (!thing_->action(name)) {
                        refusal = Refusal::not_found;
                    }
                    break;
                case Target::event:
                    if (!thing_->event(name)) {
                        refusal = Refusal::not_found;
                    }
                    break;
            }
            if (refusal) {
                bad(std::string(debug(*refusal)) + " for '" + name + "'");
                return error_response(name, *target, *refusal);
            }
            broker_->add(subscription_, thing_->name(), name);
        }
        return std::nullopt;
    }

    std::shared_ptr<MessageBroker> broker_;
    std::shared_ptr<const ThingHandle> thing_;
    std::shared_ptr<WebSocketPeer> peer_;
    Subscription subscription_;
    std::mutex mutex_;
    bool gone_ = false;
    bool closing_ = false;
};

}  // namespace

const ThingHandle* websocket_thing(const Runtime& runtime, std::string_view prefix, const Request& request) {
    const auto upgrade = request.headers.get("upgrade");
    if (request.method != "GET" || !upgrade || !equals_ignoring_case(*upgrade, "websocket")) {
        return nullptr;
    }
    std::string_view path = request.path;
    if (!path.starts_with(prefix)) {
        return nullptr;
    }
    path.remove_prefix(prefix.size());
    if (!path.starts_with('/') || !path.ends_with("/ws") || path.size() < 4) {
        return nullptr;
    }
    return runtime.thing(path.substr(1, path.size() - 4));
}

json::Value error_response(std::string_view name, Target target, Refusal refusal) {
    json::Value error = json::Value::object();
    if (refusal == Refusal::not_found) {
        error["status"] = "404";
        error["type"] = std::string(webthing_error_url) + "#not-found";
        error["title"] = "Not Found";
        error["detail"] =
            "No " + std::string(noun(target)) + " found with the name '" + std::string(name) + "'.";
    } else {
        error["status"] = "403";
        error["type"] = std::string(webthing_error_url) + "#not-observable";
        error["title"] = "Not Observable";
        error["detail"] = "Property '" + std::string(name) + "' is not observable.";
    }
    json::Value response = json::Value::object();
    response["messageType"] = "response";
    response["operation"] = operation(target);
    response["name"] = name;
    response["error"] = std::move(error);
    return response;
}

json::Value relay(const Message& message) {
    json::Value out = json::Value::object();
    json::Value data = json::Value::object();
    switch (message.kind) {
        case MessageKind::property:
            out["messageType"] = "propertyStatus";
            data[message.affordance] = message.payload;
            break;
        case MessageKind::action:
            out["messageType"] = "actionStatus";
            data["action name"] = message.affordance;
            data["status"] = message.payload;
            break;
        case MessageKind::event: {
            // A Mozilla Web Thing API dialect.
            out["messageType"] = "event";
            json::Value event = json::Value::object();
            event["data"] = message.payload;
            event["timestamp"] = rfc3339(message.time, 6);
            data[message.affordance] = std::move(event);
            break;
        }
    }
    out["data"] = std::move(data);
    return out;
}

std::shared_ptr<WebSocketSession> open_session(std::shared_ptr<MessageBroker> broker,
                                               std::shared_ptr<const ThingHandle> thing,
                                               std::shared_ptr<WebSocketPeer> peer) {
    auto session = std::make_shared<ThingSocket>(std::move(broker), std::move(thing), std::move(peer));
    session->start();
    return session;
}

}  // namespace teta::wot::http::detail
