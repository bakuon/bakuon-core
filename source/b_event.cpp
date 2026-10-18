#include "b_event.h"
#include "b_connection.h"

#include <algorithm>
#include <memory>

namespace bakuon::core {

EventBus::EventBus() noexcept
    : m_state(std::make_shared<State>())
{
}

EventBus::~EventBus() = default;

std::shared_ptr<IConnection> EventBus::subscribe(std::string_view event_name, void* user,
                                                 Handler handler)
{
    if (event_name.empty()) {
        fail("subscribe: event name is empty");
        return {};
    }
    if (handler == nullptr) {
        fail("subscribe: handler is null");
        return {};
    }

    const std::uint64_t id = m_state->next_id++;
    Channel& channel       = m_state->channels[std::string(event_name)];
    channel.slots.push_back(Slot{id, user, handler});
    clear_error();

    // weak：总线先于 Connection 销毁时，disconnect 是安全空操作。
    auto cnn = Connection(
        [weak = std::weak_ptr<State>(m_state), name = std::string(event_name), id] {
            if (const auto locked = weak.lock()) {
                const auto it = locked->channels.find(name);
                if (it == locked->channels.end()) {
                    return;
                }
                auto& slots = it->second.slots;
                slots.erase(std::remove_if(slots.begin(),
                                           slots.end(),
                                           [id](const Slot& s) { return s.id == id; }),
                            slots.end());
                if (slots.empty()) {
                    locked->channels.erase(it);
                }
            }
        });

    return std::shared_ptr<Connection>(&cnn);
}

std::uint32_t EventBus::publish(std::string_view event_name, const void* data, std::uint32_t size)
{
    if (event_name.empty()) {
        fail("publish: event name is empty");
        return 0;
    }
    if (size > 0 && data == nullptr) {
        fail("publish: data is null but size > 0");
        return 0;
    }

    const auto it = m_state->channels.find(std::string(event_name));
    if (it == m_state->channels.end()) {
        clear_error();
        return 0;
    }

    // 快照：回调里允许 subscribe / unsubscribe 同一事件。
    const std::vector<Slot> snapshot = it->second.slots;
    std::uint32_t invoked            = 0;
    for (const Slot& slot : snapshot) {
        if (slot.handler != nullptr) {
            slot.handler(slot.user, data, size);
            ++invoked;
        }
    }
    clear_error();
    return invoked;
}

std::uint32_t EventBus::subscriber_count(std::string_view event_name) const
{
    const auto it = m_state->channels.find(std::string(event_name));
    if (it == m_state->channels.end()) {
        return 0;
    }
    return static_cast<std::uint32_t>(it->second.slots.size());
}

} // namespace bakuon::core
