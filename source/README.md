## 以后要补的点

1. **非平凡组件。** 现在只 `memcpy`。在注册描述里加 `construct` / `destroy` / `move` 三个函数指针，由插件侧生成、引擎在 `PluginSlot` 构造和析构时调用。插件卸载前必须先 `unregister_component`，否则这些函数指针悬空。POD 没有这个问题。
2. **分配方式。** 现在每个实例一次 `operator new`，换来指针稳定。热组件应改成按 `stride` 分块的 SoA，空槽走 freelist；同时把失效规则改成和 EnTT 一样：同池 `swap_and_pop` 会使指针失效。`component_traits::in_place_delete` 只在需要稳定下标时打开。
3. **查询。** 现在选最小的池，对其余池做 `contains`，最多 16 个组件。热路径改为持有 `storage<PluginSlot>*` 的 `entt::runtime_view`，并加上 exclude。真正的热对再用 `group`，但运行时 id 的 group 要在两个 storage 都创建之后再绑定。
4. **遍历时改结构。** EnTT 不允许边遍历边增删同一个池。加命令缓冲：回调里的 `emplace` / `remove` / `destroy` 记下来，`each` 返回后统一回放。这是做系统调度之前的前置条件。
5. **脏标记。** `replace` 之后给该 storage 调 `patch` 或自增版本号，复制和网络只发送脏组件。不要在插件头里暴露 `on_update` 的 sink。
6. **存档。** 遍历每个 `Record`，写出 `Entity::value()`、组件名、`size` 和字节。读回时用 `registry.create(hint)` 保住 id，再按名字注册并 `emplace`。名字是契约，不能改成编译器相关的 `type_hash`。
7. **关系。** `Parent { Entity to; }` 这种 POD 里的 `Entity` 在目标销毁后会变成过期 id。`valid()` 能看出来，但不会自动清空。要做反向索引，或在销毁时扫引用该实体的池。
8. **系统顺序。** 让插件注册 `void (*)(World&)`，引擎按名字做 before/after 拓扑排序后调用。插件不拥有主循环。
9. **线程。** `basic_registry` 不能并发写。每个 job 写命令缓冲，帧末在拥有 registry 的线程回放。不要把 `World*` 直接交给工作线程。
10. **id 宽度。** `ComponentId` 必须等于 `ENTT_ID_TYPE`（默认 `uint32_t`），代码里有 `static_assert`。FNV-1a 冲突会在注册时拒绝，处理办法是改名字，不要静默线性探测，否则存档 id 会随注册顺序变化。