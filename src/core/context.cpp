#include "teta/wot/context.hpp"

#include <thread>

namespace teta::wot {

namespace {

thread_local const InvocationScope* current_scope = nullptr;

}  // namespace

InvocationScope::Enter::Enter(const InvocationScope& scope)
    : previous_(current_scope), capture_(scope.logs_) {
    current_scope = &scope;
}

InvocationScope::Enter::~Enter() {
    current_scope = previous_;
}

InvocationScope InvocationScope::fake() {
    const Uuid id = Uuid::v4();
    return {id, id, CancelToken(), std::make_shared<LogBuffer>(id)};
}

std::optional<InvocationScope> InvocationScope::current() {
    if (current_scope) {
        return *current_scope;
    }
    return std::nullopt;
}

InvocationScope InvocationScope::child() const {
    const Uuid id = Uuid::v4();
    return {id, lock_owner_, CancelToken(), std::make_shared<LogBuffer>(id)};
}

void cancellable_sleep(std::chrono::nanoseconds duration) {
    if (current_scope) {
        current_scope->cancel_token().sleep(duration);
    } else {
        std::this_thread::sleep_for(duration);
    }
}

void check_cancelled() {
    if (current_scope) {
        current_scope->cancel_token().check();
    }
}

ActionCtx::ActionCtx(InvocationScope scope, std::string thing, std::shared_ptr<GlobalLock> lock,
                     Server server, std::shared_ptr<WorkerPool> pool)
    : scope_(std::move(scope)),
      thing_(std::move(thing)),
      lock_(std::move(lock)),
      server_(std::move(server)),
      pool_(pool ? std::move(pool) : WorkerPool::shared()) {}

ActionCtx ActionCtx::detached(std::string thing) {
    return {InvocationScope::fake(), std::move(thing), nullptr, Server::detached(), WorkerPool::shared()};
}

GlobalLock::Guard ActionCtx::hold_global_lock() const {
    if (!lock_) {
        throw GlobalLockNotEnabled();
    }
    return lock_->acquire(scope_.lock_owner());
}

}  // namespace teta::wot
