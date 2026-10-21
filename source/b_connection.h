#pragma once

#include <type_traits>

namespace bakuon::core {

/**
 * @brief RAII 信号连接守卫。
 *
 * @details 构造时持有一组 C 风格的函数指针与一个不透明 context，析构时自动断开；
 * 支持提前 dismiss() 放弃自动断开、支持移动、禁止拷贝——同一份连接只能有一个所有者。
 *
 * 本类不再持有 std::function。所有回调都是 `void (*)(void*)` + `void* context`，
 * 因此可以安全地作为未来公共接口的一部分暴露；context 的分配/释放由创建它的模块
 * 通过 destroy 回调负责，Connection 本身只做生命周期编排。
 *
 * @code
 *   auto* ctx = new MyContext{...};
 *   Connection conn{ctx, &disconnect_my_thing, nullptr, &delete_my_context};
 *   // conn 存活期间连接有效；析构时 disconnect_my_thing(ctx)，随后 delete_my_context(ctx)
 * @endcode
 *
 * ## disconnector 与 onDismiss 为什么是两个独立的回调
 * 第一版实现只有一个"断开回调"，dismiss() 简单地把它设为空——这在回调本身
 * 只是"从某个容器里摘除一条记录"时是安全的，但当断开回调背后还挂着一份
 * 堆上分配的资源时（典型如 Registry::bindSink() 里为每次订阅分配的 Slot<T>，
 * entt::sink 内部保存的是指向它的裸指针，不是 shared_ptr），"忘记"断开回调
 * 会连带丢弃该回调闭包里捕获的最后一份 shared_ptr 引用——资源立刻被释放，
 * 而 entt 那边浑然不知，下一次事件触发就是一次悬空访问。
 *
 * 现在的设计把"断开"和"放弃自动断开"拆成两条独立的路径：
 *  - disconnector：真正执行断开动作（比如 entt::connection::release()），
 *    执行完毕后它背后的资源可以安全释放；
 *  - onDismiss：dismiss() 时执行，职责仅仅是"把背后资源的所有权转移到一个
 *    活得比这个 Connection 更久的地方"，不释放任何东西。
 * 两条路径最终都会调用 destroy(context)，但只会在其中一条之后执行一次。
 *
 * ## ABI 约束
 * - 回调必须是普通函数指针，不能是 std::function/成员函数指针。
 * - 回调不得抛异常；本类在调用处做了 catch-all 兜底，但不要依赖它。
 * - context 的生命周期由 destroy 管；如果 destroy 为空，Connection 只做
 *   "通知"而不会释放 context（非拥有模式）。
 */
class Connection
{
public:
    using Callback = void (*)(void*);

    Connection() noexcept = default;

    /**
     * @param context      交给回调的不透明指针
     * @param disconnector 断开连接时执行；可为空（此时连接不可断开）
     * @param on_dismiss   dismiss() 时执行；可为空
     * @param destroy      连接结束（断开或 dismiss）后释放 context；可为空（非拥有）
     */
    Connection(void* context, Callback disconnector, Callback on_dismiss = nullptr,
               Callback destroy = nullptr) noexcept
        : m_context(context)
        , m_disconnector(disconnector)
        , m_on_dismiss(on_dismiss)
        , m_destroy(destroy)
    {
    }

    Connection(const Connection&)            = delete;
    Connection& operator=(const Connection&) = delete;

    Connection(Connection&& other) noexcept
        : m_context(other.m_context)
        , m_disconnector(other.m_disconnector)
        , m_on_dismiss(other.m_on_dismiss)
        , m_destroy(other.m_destroy)
    {
        other.reset();
    }

    Connection& operator=(Connection&& other) noexcept
    {
        if (this != &other) {
            disconnect();
            m_context      = other.m_context;
            m_disconnector = other.m_disconnector;
            m_on_dismiss   = other.m_on_dismiss;
            m_destroy      = other.m_destroy;
            other.reset();
        }
        return *this;
    }

    ~Connection() { disconnect(); }

    /// 主动断开连接；幂等，重复调用/对一个从未连接过的对象调用都是安全的空操作。
    void disconnect() noexcept
    {
        if (m_disconnector != nullptr) {
            const Callback fn = m_disconnector;
            m_disconnector    = nullptr;
            m_on_dismiss      = nullptr;
            try {
                fn(m_context);
            } catch (...) {
                // 回调契约要求不抛；这里兜底，避免异常穿过 ABI/析构函数。
            }
        }
        release();
    }

    /**
     * @brief 放弃自动断开：析构时不再调用断开回调，转而调用 on_dismiss()（若提供）
     * 把背后资源的所有权移交出去，然后释放 Connection 自己的 context。
     */
    void dismiss() noexcept
    {
        if (m_on_dismiss != nullptr) {
            const Callback fn = m_on_dismiss;
            m_disconnector    = nullptr;
            m_on_dismiss      = nullptr;
            try {
                fn(m_context);
            } catch (...) {
            }
        }
        release();
    }

    [[nodiscard]] bool is_connected() const noexcept { return m_disconnector != nullptr; }

private:
    void release() noexcept
    {
        if (m_destroy != nullptr) {
            const Callback fn = m_destroy;
            m_destroy         = nullptr;
            try {
                fn(m_context);
            } catch (...) {
            }
        }
        m_context = nullptr;
    }

    void reset() noexcept
    {
        m_context      = nullptr;
        m_disconnector = nullptr;
        m_on_dismiss   = nullptr;
        m_destroy      = nullptr;
    }

    void* m_context{nullptr};
    Callback m_disconnector{nullptr};
    Callback m_on_dismiss{nullptr};
    Callback m_destroy{nullptr};
};

static_assert(std::is_standard_layout_v<Connection>, "Connection must be standard layout");

} // namespace bakuon::core
