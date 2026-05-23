#pragma once

#include "core/Types.hpp"
#include "ecs/Entity.hpp"
#include "math/Vec3.hpp"
#include <vector>
#include <string>
#include <sstream>

namespace Caffeine::Editor {

class EditorContext;

struct UIElement {
    u32 id;
    std::string name;
    f32 x, y, w, h;
    bool selected;
    
    std::string toJson() const {
        std::ostringstream oss;
        oss << "{\"id\":" << id << ",\"name\":\"" << name 
            << "\",\"x\":" << x << ",\"y\":" << y 
            << ",\"w\":" << w << ",\"h\":" << h 
            << ",\"selected\":" << (selected ? "true" : "false") << "}";
        return oss.str();
    }
};

struct ViewportInfo {
    f32 x, y, width, height;
    
    std::string toJson() const {
        std::ostringstream oss;
        oss << "{\"x\":" << x << ",\"y\":" << y 
            << ",\"width\":" << width << ",\"height\":" << height << "}";
        return oss.str();
    }
};

struct UIMapResponse {
    std::vector<UIElement> entities;
    ViewportInfo viewport;
    
    std::string toJson() const {
        std::ostringstream oss;
        oss << "{\"viewport\":" << viewport.toJson() << ",\"entities\":[";
        for (size_t i = 0; i < entities.size(); ++i) {
            if (i > 0) oss << ",";
            oss << entities[i].toJson();
        }
        oss << "]}";
        return oss.str();
    }
};

class TestUIMapper {
public:
    static UIMapResponse captureViewportState(
        ECS::World& world,
        EditorContext& ctx,
        f32 viewportX, f32 viewportY,
        f32 viewportWidth, f32 viewportHeight);
    
    static bool clickAtCoordinate(
        ECS::World& world,
        EditorContext& ctx,
        f32 screenX, f32 screenY,
        bool shiftPressed,
        bool doubleClick,
        f32 viewportX, f32 viewportY,
        f32 viewportWidth, f32 viewportHeight);
};

}
