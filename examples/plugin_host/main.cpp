// Minimal host application: depends only on the public headers and the bakuon_core library.
#include "components.h"

#include <bakuon/core/component.h>
#include <bakuon/core/host.h>

#include <cmath>
#include <cstdint>
#include <cstdio>

using namespace bakuon::core;

namespace {

bool near(float a, float b)
{
    return std::fabs(a - b) < 1e-4f;
}

int run(IHost& host, const char* plugin_path)
{
    IDomain* domain = host.world().require("main");
    if (domain == nullptr) {
        return 1;
    }
    if (host.load_plugin(plugin_path) != Status::ok) {
        std::fprintf(stderr, "load failed: %s\n", host.last_error());
        return 1;
    }
    std::printf("loaded plugin '%s'\n", host.plugin_name_at(0));

    TypedComponent<example::Position> position;
    TypedComponent<example::Velocity> velocity;
    if (TypedComponent<example::Position>::bind(domain->components(),
                                                example::position_name,
                                                position)
            != Status::ok
        || TypedComponent<example::Velocity>::bind(domain->components(),
                                                   example::velocity_name,
                                                   velocity)
               != Status::ok) {
        std::fprintf(stderr, "components were not registered by the plugin\n");
        return 1;
    }

    const example::Velocity speeds[3] = {{1.f, 0.f, 0.f}, {0.f, 2.f, 0.f}, {0.5f, 0.5f, 0.5f}};
    Entity entities[3];
    for (int i = 0; i < 3; ++i) {
        entities[i] = domain->create();
        if (position.add_default(entities[i]) != Status::ok
            || velocity.add(entities[i], speeds[i]) != Status::ok) {
            return 1;
        }
    }

    for (std::uint64_t frame = 0; frame < 10; ++frame) { // 10 x 0.1 s = 1 s
        if (host.world().run_frame(FrameContext{frame, 0.1}) != Status::ok) {
            return 1;
        }
    }

    int rc = 0;
    for (int i = 0; i < 3; ++i) {
        const example::Position* p = position.get_const(entities[i]);
        if (p == nullptr) {
            return 1;
        }
        std::printf("entity %d: (%.3f, %.3f, %.3f)\n", i, p->x, p->y, p->z);
        if (!near(p->x, speeds[i].x) || !near(p->y, speeds[i].y) || !near(p->z, speeds[i].z)) {
            rc = 1;
        }
    }

    // The serializer hook exported by the plugin runs inside the plugin's module.
    ComponentInfo info{};
    info.struct_size = static_cast<std::uint32_t>(sizeof(info));
    if (domain->components().describe(position.id(), &info) == Status::ok
        && info.serializer != nullptr) {
        unsigned char buf[sizeof(example::Position)];
        std::size_t n = 0;
        if (info.serializer(position.get_const(entities[0]), buf, sizeof(buf), &n, info.hook_user)
            == 0) {
            std::printf("serialized %zu bytes through the plugin's hook\n", n);
        }
    }

    if (host.unload_plugin("movement") != Status::ok) {
        return 1;
    }
    const bool position_left = domain->components().find(example::position_name).valid();
    const bool velocity_left = domain->components().find(example::velocity_name).valid();
    std::printf("after unload: Position registered=%s, Velocity registered=%s\n",
                position_left ? "yes" : "no",
                velocity_left ? "yes" : "no");
    if (position_left || !velocity_left) { // hooked type removed, data-only type kept
        rc = 1;
    }
    return rc;
}

} // namespace

int main(int argc, char** argv)
{
    if (argc < 2) {
        std::fprintf(stderr, "usage: plugin_host <path-to-plugin-library>\n");
        return 2;
    }
    if (bakuon_host_abi_version() != IHost::abi_current) {
        std::fprintf(stderr, "host library ABI mismatch\n");
        return 1;
    }
    IHost* host = bakuon_host_create();
    if (host == nullptr) {
        std::fprintf(stderr, "could not create host\n");
        return 1;
    }
    const int rc = run(*host, argv[1]);
    bakuon_host_destroy(host);
    return rc;
}
