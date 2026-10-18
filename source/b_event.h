#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include <bakuon/core/event.h>

// 只给引擎编译单元包含。插件禁止包含本头。

namespace bakuon::core {

/**
 * @brief IEventBus 的具体实现。
 *
 * ## 线程
 * subscribe / publish 应在同一线程调用（与 World / Scheduler 一致）。
 * 不做内部加锁。
 *
 * ## 重入
 * publish 期间对同一事件的 subscribe / unsubscribe 是安全的：
 * 回调列表先做快照，本次 publish 只遍历快照。
 *
 * ## 生命周期
 * Connection 持 weak 引用到内部状态；总线先销毁时 disconnect 为空操作。
 */
class EventBus final : public IEventBus
{
public:
    EventBus() noexcept;
    ~EventBus() override;

    EventBus(const EventBus&)            = delete;
    EventBus& operator=(const EventBus&) = delete;
    EventBus(EventBus&&)                 = delete;
    EventBus& operator=(EventBus&&)      = delete;

    [[nodiscard]] std::uint32_t abi_version() const noexcept override { return event_abi_version; }

    [[nodiscard]] std::shared_ptr<IConnection> subscribe(std::string_view event_name, void* user,
                                                         Handler handler) override;

    std::uint32_t publish(std::string_view event_name, const void* data,
                          std::uint32_t size) override;

    [[nodiscard]] std::uint32_t subscriber_count(std::string_view event_name) const override;

    [[nodiscard]] const char* last_error() const override { return m_error.c_str(); }

private:
    struct Slot
    {
        std::uint64_t id = 0;
        void* user       = nullptr;
        Handler handler  = nullptr;
    };

    struct Channel
    {
        std::vector<Slot> slots;
    };

    /// 用 shared_ptr 管理，使 Connection 的 weak 在总线析构后仍可安全 lock 失败。
    struct State
    {
        std::unordered_map<std::string, Channel> channels;
        std::uint64_t next_id = 1;
    };

    void fail(const char* message) { m_error = message; }
    void clear_error() { m_error.clear(); }

private:
    std::shared_ptr<State> m_state;
    std::string m_error;
};

} // namespace bakuon::core
