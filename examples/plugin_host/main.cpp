/**
 * plugin_host — 加载 example 插件 DLL，跑几帧 MovementSystem。
 *
 * 用法：
 *   plugin_host [path/to/plugin_movement.so]
 * 未传路径时使用编译期注入的 BAKUON_EXAMPLE_PLUGIN_PATH。
 */

#include <bakuon/core/domain.h>
#include <bakuon/core/event.h>
#include <bakuon/core/plugin.h>
#include <bakuon/core/plugin_api.h>
#include <bakuon/core/service.h>
#include <bakuon/core/bakuon.h>

#include "b_scheduler.h"
#include "b_world.h"

#include <cstdint>
#include <iostream>
#include <string>
#include <string_view>

#if defined(_WIN32)
#    define WIN32_LEAN_AND_MEAN
#    include <windows.h>
using LibraryHandle = HMODULE;
#else
#    include <dlfcn.h>
using LibraryHandle = void*;
#endif

namespace {

struct Position
{
    float x = 0.f;
    float y = 0.f;
};

struct Velocity
{
    float x = 0.f;
    float y = 0.f;
};

struct MovementTickEvent
{
    float dt = 0.f;
    std::uint32_t entity_count = 0;
};

class ConsoleLogger final
{
public:
    static constexpr std::uint32_t abi   = 1;
    static constexpr std::string_view id = "core.Logger";

    void info(std::string_view msg)
    {
        std::cout << "[log] " << msg << '\n';
        ++count;
    }
    int count = 0;
};

// 插件侧 ILogger 是虚接口；宿主用同布局服务名注册时，插件 try_get 得到的是
// 插件头里的 ILogger*。为简单起见，本示例在插件内把 Logger 当可选服务；
// 宿主注册一个匹配 id/abi 的对象。跨 DLL 虚表要求编译器一致。
class HostLogger final
{
public:
    static constexpr std::uint32_t abi   = 1;
    static constexpr std::string_view id = "core.Logger";

    virtual ~HostLogger() = default;
    virtual void info(std::string_view msg)
    {
        std::cout << "[host-log] " << msg << '\n';
    }
};

LibraryHandle load_library(const char* path)
{
#if defined(_WIN32)
    return ::LoadLibraryA(path);
#else
    return ::dlopen(path, RTLD_NOW | RTLD_LOCAL);
#endif
}

void close_library(LibraryHandle handle)
{
    if (handle == nullptr) {
        return;
    }
#if defined(_WIN32)
    ::FreeLibrary(handle);
#else
    ::dlclose(handle);
#endif
}

void* load_symbol(LibraryHandle handle, const char* name)
{
#if defined(_WIN32)
    return reinterpret_cast<void*>(::GetProcAddress(handle, name));
#else
    return ::dlsym(handle, name);
#endif
}

const char* library_error()
{
#if defined(_WIN32)
    return "LoadLibrary/GetProcAddress failed";
#else
    const char* err = ::dlerror();
    return err != nullptr ? err : "unknown";
#endif
}

} // namespace

int main(int argc, char* argv[])
{
    const char* plugin_path =
#if defined(BAKUON_EXAMPLE_PLUGIN_PATH)
        argc > 1 ? argv[1] : BAKUON_EXAMPLE_PLUGIN_PATH;
#else
        argc > 1 ? argv[1] : nullptr;
#endif

    if (plugin_path == nullptr || plugin_path[0] == '\0') {
        std::cerr << "usage: plugin_host <path-to-plugin_movement>\n";
        return 1;
    }

    std::cout << "bakuon " << bakuon::versionString() << '\n';
    std::cout << "loading plugin: " << plugin_path << '\n';

    LibraryHandle lib = load_library(plugin_path);
    if (lib == nullptr) {
        std::cerr << "dlopen failed: " << library_error() << '\n';
        return 2;
    }

    auto create = reinterpret_cast<bakuon::core::PluginCreateFn>(
        load_symbol(lib, bakuon::core::plugin_create_symbol));
    auto destroy = reinterpret_cast<bakuon::core::PluginDestroyFn>(
        load_symbol(lib, bakuon::core::plugin_destroy_symbol));

    if (create == nullptr || destroy == nullptr) {
        std::cerr << "missing export symbols: " << library_error() << '\n';
        close_library(lib);
        return 3;
    }

    int exit_code = 0;
    float final_x = 0.f;
    float final_y = 0.f;
    int tick_events = 0;

    // World / System 必须在 dlclose 之前析构：System 实现位于插件 .so，
    // ~Scheduler 会 delete ISystem 并调用虚函数，此时映射须仍有效。
    {
        bakuon::core::World world;
        HostLogger logger;
        (void)world.services().add(HostLogger::id, static_cast<void*>(&logger), HostLogger::abi);

        auto on_tick = [&](const MovementTickEvent& e) {
            ++tick_events;
            std::cout << "  MovementTick dt=" << e.dt << " entities=" << e.entity_count << '\n';
        };
        bakuon::core::Connection tick_conn = bakuon::core::subscribe<MovementTickEvent>(
            world.events(), "game.MovementTick", on_tick);

        bakuon::core::PluginContext ctx(world);
        bakuon::core::IPlugin* plugin = create();
        if (plugin == nullptr) {
            std::cerr << "bakuon_create_plugin returned null\n";
            close_library(lib);
            return 4;
        }

        std::cout << "plugin: " << plugin->name() << " v" << plugin->version() << '\n';
        if (plugin->abi_version() != bakuon::core::plugin_abi_version) {
            std::cerr << "plugin abi mismatch\n";
            destroy(plugin);
            close_library(lib);
            return 5;
        }

        plugin->on_load(ctx);

        bakuon::core::Domain* game = world.find("game");
        if (game == nullptr) {
            std::cerr << "domain 'game' not created by plugin\n";
            plugin->on_unload();
            destroy(plugin);
            close_library(lib);
            return 6;
        }

        const bakuon::core::Entity e = game->create();
        auto pos = bakuon::core::component<Position>(*game, "game.Position");
        auto vel = bakuon::core::component<Velocity>(*game, "game.Velocity");
        if (!pos || !vel) {
            std::cerr << "component register failed: " << game->last_error() << '\n';
            plugin->on_unload();
            destroy(plugin);
            close_library(lib);
            return 7;
        }
        (void)bakuon::core::emplace(*game, e, pos, Position{0.f, 0.f});
        (void)bakuon::core::emplace(*game, e, vel, Velocity{10.f, 20.f});

        constexpr float dt = 0.5f;
        for (int i = 0; i < 3; ++i) {
            std::cout << "tick " << i << '\n';
            world.tick(dt);
        }

        if (const Position* p = bakuon::core::get(*game, e, pos)) {
            final_x = p->x;
            final_y = p->y;
            std::cout << "final position: (" << final_x << ", " << final_y << ")\n";
        } else {
            exit_code = 8;
        }
        std::cout << "movement events received: " << tick_events << '\n';

        // 先卸插件实例，再让 World 析构（仍持有 .so）
        world.scheduler().unregister_system("game.Movement");
        plugin->on_unload();
        destroy(plugin);
        plugin = nullptr;
    } // ~World：Scheduler / Domain / EventBus

    close_library(lib);

    if (exit_code == 8) {
        std::cerr << "position check failed\n";
        return 8;
    }
    // 3 * 0.5 * (10, 20) => (15, 30)
    if (final_x < 14.9f || final_x > 15.1f || final_y < 29.9f || final_y > 30.1f) {
        std::cerr << "position check failed\n";
        return 8;
    }
    if (tick_events != 3) {
        std::cerr << "event count check failed\n";
        return 9;
    }

    std::cout << "plugin_host: OK\n";
    return 0;
}
