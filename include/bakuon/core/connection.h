#pragma once

namespace bakuon::core {

class IConnection
{
public:
    virtual ~IConnection() = 0;

    /// 主动断开连接；幂等，重复调用/对一个从未连接过的对象调用都是安全的空操作。
    virtual void disconnect() = 0;

    /**
     * @brief 放弃自动断开：析构时不再调用断开回调，转而调用 onDismiss()（若提供）
     * 把背后资源的所有权移交出去，然后彻底放弃对这次连接的一切控制权——调用方
     * 之后无法再通过这个 Connection 对象重新断开它，这是有意的：dismiss() 的
     * 语义就是"我确定这个订阅应该和它的宿主活得一样久，不需要再被单独管理"。
     */
    virtual void dismiss() noexcept = 0;

    [[nodiscard]] virtual bool isConnected() const noexcept = 0;
};

} // namespace bakuon::core
