#pragma once
// Shared by the plugin and the application: plain structs and registration names.
namespace example {

struct Position
{
    float x, y, z;
};
struct Velocity
{
    float x, y, z;
};

inline constexpr const char* position_name = "example.Position";
inline constexpr const char* velocity_name = "example.Velocity";

} // namespace example
