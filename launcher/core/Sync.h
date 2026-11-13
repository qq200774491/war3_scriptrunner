#pragma once

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <memory>
#include <mutex>
#include <optional>
#include <utility>

namespace scriptroom {

/// 一次性停止信号：可跨线程请求，也可带超时等待。
class StopSignal {
public:
    void request() {
        {
            std::lock_guard<std::mutex> Lock(Mutex);
            Requested.store(true);
        }
        Wake.notify_all();
    }

    bool is_requested() const { return Requested.load(); }

    /// 等待至请求停止或超时；返回是否已请求停止。
    bool wait_for(std::chrono::milliseconds Timeout) const {
        std::unique_lock<std::mutex> Lock(Mutex);
        return Wake.wait_for(Lock, Timeout, [this] { return Requested.load(); });
    }

private:
    mutable std::mutex Mutex;
    mutable std::condition_variable Wake;
    std::atomic<bool> Requested{false};
};

using StopHandle = std::shared_ptr<StopSignal>;

/// 有界多生产者队列。try_send 在队列已满或已关闭时失败且不消耗传入的值。
template <typename T> class Channel {
public:
    explicit Channel(std::size_t Capacity) : Limit(Capacity) {}

    bool try_send(T &&Value) {
        {
            std::lock_guard<std::mutex> Lock(Mutex);
            if (Closed || Items.size() >= Limit) {
                return false;
            }
            Items.push_back(std::move(Value));
        }
        Readable.notify_one();
        return true;
    }

    /// 阻塞直到有空位；通道关闭时返回 false。
    bool send(T &&Value) {
        {
            std::unique_lock<std::mutex> Lock(Mutex);
            Writable.wait(Lock, [this] { return Closed || Items.size() < Limit; });
            if (Closed) {
                return false;
            }
            Items.push_back(std::move(Value));
        }
        Readable.notify_one();
        return true;
    }

    /// 阻塞接收；通道关闭且已取空时返回 nullopt。
    std::optional<T> recv() {
        std::unique_lock<std::mutex> Lock(Mutex);
        Readable.wait(Lock, [this] { return Closed || !Items.empty(); });
        return take(Lock);
    }

    std::optional<T> recv_for(std::chrono::milliseconds Timeout) {
        std::unique_lock<std::mutex> Lock(Mutex);
        Readable.wait_for(Lock, Timeout, [this] { return Closed || !Items.empty(); });
        return take(Lock);
    }

    std::optional<T> try_recv() {
        std::unique_lock<std::mutex> Lock(Mutex);
        return take(Lock);
    }

    /// 关闭后不再接收新值；已入队的值仍可读完。
    void close() {
        {
            std::lock_guard<std::mutex> Lock(Mutex);
            Closed = true;
        }
        Readable.notify_all();
        Writable.notify_all();
    }

    bool is_closed() const {
        std::lock_guard<std::mutex> Lock(Mutex);
        return Closed;
    }

    std::size_t size() const {
        std::lock_guard<std::mutex> Lock(Mutex);
        return Items.size();
    }

private:
    std::optional<T> take(std::unique_lock<std::mutex> &Lock) {
        if (Items.empty()) {
            return std::nullopt;
        }
        std::optional<T> Value(std::move(Items.front()));
        Items.pop_front();
        Lock.unlock();
        Writable.notify_one();
        return Value;
    }

    mutable std::mutex Mutex;
    std::condition_variable Readable;
    std::condition_variable Writable;
    std::deque<T> Items;
    std::size_t Limit;
    bool Closed = false;
};

/// 带版本号的共享状态：读者可复制当前值，也可等待变化。
template <typename T> class Watch {
public:
    explicit Watch(T Initial) : Current(std::move(Initial)) {}

    T borrow() const {
        std::lock_guard<std::mutex> Lock(Mutex);
        return Current;
    }

    void send_replace(T Value) {
        {
            std::lock_guard<std::mutex> Lock(Mutex);
            Current = std::move(Value);
            ++Version;
        }
        Changed.notify_all();
    }

    std::uint64_t version() const {
        std::lock_guard<std::mutex> Lock(Mutex);
        return Version;
    }

    /// 等待版本号越过 Seen；成功时更新 Seen 并返回 true，超时返回 false。
    bool wait_changed(std::uint64_t &Seen, std::chrono::milliseconds Timeout) const {
        std::unique_lock<std::mutex> Lock(Mutex);
        if (!Changed.wait_for(Lock, Timeout, [&] { return Version != Seen; })) {
            return false;
        }
        Seen = Version;
        return true;
    }

private:
    mutable std::mutex Mutex;
    mutable std::condition_variable Changed;
    T Current;
    std::uint64_t Version = 0;
};

} // namespace scriptroom
