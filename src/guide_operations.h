#pragma once
#include "guide_store.h"

// Shared by the window recorder and the game-free command interface.
namespace guides {
inline void bindLocation(Value& next, const std::string& target, const std::string& location) {
    auto merged = merge(defaults, next);
    for (const auto& b : list(merged, "bindings"))
        if (!removed(b) && b.at("target").str() == target && b.at("location").str() == location) return;
    auto b = Value::dict(); b["id"] = newId("approach"); b["target"] = target; b["location"] = location;
    upsert(next, "bindings", b);
}
inline std::string recordLocation(Value& next, const Value& location) {
    auto merged = merge(defaults, next);
    for (const auto& old : list(merged, "locations"))
        if (!removed(old) && !locationDisabled(old) && old.at("zone").str() == location.at("zone").str() &&
            std::hypot(old.at("x").num()-location.at("x").num(), old.at("z").num()-location.at("z").num()) < 1.0 &&
            std::abs(old.at("y").num()-location.at("y").num()) < 2.0) return id(old);
    upsert(next, "locations", location); return id(location);
}
}
