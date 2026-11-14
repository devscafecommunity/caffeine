#pragma once

#include "core/Types.hpp"
#include "ecs/Entity.hpp"
#include "ecs/World.hpp"
#include <string>
#include <sstream>

namespace Caffeine::Editor {

class EditorContext;

class TestRequestHandler {
public:
    struct Request {
        std::string cmd;
        u32 id;
        f32 x, y;
        bool shift;
        bool double_click;
    };
    
    struct Response {
        bool success;
        u32 id;
        std::string action;
        std::string data;
        
        std::string toJson() const {
            std::ostringstream oss;
            oss << "{\"success\":" << (success ? "true" : "false")
                << ",\"id\":" << id
                << ",\"action\":\"" << action << "\""
                << ",\"data\":" << data << "}";
            return oss.str();
        }
    };
    
    static bool tryParseRequest(const std::string& line, Request& outRequest);
    
    static Response handleRequest(
        const Request& req,
        ECS::World& world,
        EditorContext& ctx,
        f32 viewportX, f32 viewportY,
        f32 viewportWidth, f32 viewportHeight);
    
private:
    static Response handleGetUIMap(
        ECS::World& world,
        EditorContext& ctx,
        u32 requestId,
        f32 viewportX, f32 viewportY,
        f32 viewportWidth, f32 viewportHeight);
    
    static Response handleClick(
        const Request& req,
        ECS::World& world,
        EditorContext& ctx,
        f32 viewportX, f32 viewportY,
        f32 viewportWidth, f32 viewportHeight);
    
    static Response handleGetState(
        ECS::World& world,
        EditorContext& ctx,
        u32 requestId);
};

}
