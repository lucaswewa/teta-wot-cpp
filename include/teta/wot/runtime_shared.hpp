// What every Thing of a runtime shares (internal).

#pragma once

#include <memory>

#include "teta/wot/broker.hpp"
#include "teta/wot/invocation.hpp"
#include "teta/wot/lock.hpp"
#include "teta/wot/log.hpp"
#include "teta/wot/pool.hpp"
#include "teta/wot/server.hpp"

namespace teta::wot {

struct RuntimeShared {
    /// The global lock, if enabled.
    std::shared_ptr<GlobalLock> lock;
    /// The level of the "Global lock was busy" log line.
    log::Level lock_log_level = log::Level::info;
    std::shared_ptr<MessageBroker> broker;
    std::shared_ptr<InvocationManager> invocations;
    Server server;
    std::shared_ptr<WorkerPool> pool;
};

}  // namespace teta::wot
