#include "editor/TestRequestHandler.hpp"
#include "editor/TestUIMapper.hpp"
#include "editor/EditorContext.hpp"
#include "ecs/World.hpp"
#include <iostream>
#include <sstream>
#include <algorithm>

namespace Caffeine::Editor {

bool TestRequestHandler::tryParseRequest(const std::string& line, Request& outRequest) {
    if (line.empty() || line[0] != '{') {
        return false;
    }
    
    try {
        outRequest.cmd = "unknown";
        outRequest.id = 0;
        outRequest.x = 0;
        outRequest.y = 0;
        outRequest.shift = false;
        outRequest.double_click = false;
        
        if (line.find("\"cmd\":\"get_ui_map\"") != std::string::npos) {
            outRequest.cmd = "get_ui_map";
        } else if (line.find("\"cmd\":\"click\"") != std::string::npos) {
            outRequest.cmd = "click";
        } else if (line.find("\"cmd\":\"get_state\"") != std::string::npos) {
            outRequest.cmd = "get_state";
        }
        
        size_t idPos = line.find("\"id\":");
        if (idPos != std::string::npos) {
            outRequest.id = std::stoul(line.substr(idPos + 5));
        }
        
        size_t xPos = line.find("\"x\":");
        if (xPos != std::string::npos) {
            outRequest.x = std::stof(line.substr(xPos + 4));
        }
        
        size_t yPos = line.find("\"y\":");
        if (yPos != std::string::npos) {
            outRequest.y = std::stof(line.substr(yPos + 4));
        }
        
        outRequest.shift = line.find("\"shift\":true") != std::string::npos;
        outRequest.double_click = line.find("\"double\":true") != std::string::npos;
        
        return true;
    } catch (...) {
        return false;
    }
}

TestRequestHandler::Response TestRequestHandler::handleRequest(
    const Request& req,
    ECS::World& world,
    EditorContext& ctx,
    f32 viewportX, f32 viewportY,
    f32 viewportWidth, f32 viewportHeight) {
    
    if (req.cmd == "get_ui_map") {
        return handleGetUIMap(world, ctx, req.id, viewportX, viewportY, viewportWidth, viewportHeight);
    } else if (req.cmd == "click") {
        return handleClick(req, world, ctx, viewportX, viewportY, viewportWidth, viewportHeight);
    } else if (req.cmd == "get_state") {
        return handleGetState(world, ctx, req.id);
    }
    
    Response resp;
    resp.success = false;
    resp.id = req.id;
    resp.action = "unknown";
    resp.data = "{}";
    return resp;
}

TestRequestHandler::Response TestRequestHandler::handleGetUIMap(
    ECS::World& world,
    EditorContext& ctx,
    u32 requestId,
    f32 viewportX, f32 viewportY,
    f32 viewportWidth, f32 viewportHeight) {
    
    auto uiMap = TestUIMapper::captureViewportState(world, ctx, viewportX, viewportY, viewportWidth, viewportHeight);
    
    Response resp;
    resp.success = true;
    resp.id = requestId;
    resp.action = "get_ui_map";
    resp.data = uiMap.toJson();
    return resp;
}

TestRequestHandler::Response TestRequestHandler::handleClick(
    const Request& req,
    ECS::World& world,
    EditorContext& ctx,
    f32 viewportX, f32 viewportY,
    f32 viewportWidth, f32 viewportHeight) {
    
    bool success = TestUIMapper::clickAtCoordinate(
        world, ctx,
        req.x, req.y,
        req.shift,
        req.double_click,
        viewportX, viewportY,
        viewportWidth, viewportHeight
    );
    
    std::ostringstream oss;
    oss << "{\"selected_count\":" << ctx.selectedEntities.size()
        << ",\"success\":" << (success ? "true" : "false") << "}";
    
    Response resp;
    resp.success = success;
    resp.id = req.id;
    resp.action = "click";
    resp.data = oss.str();
    return resp;
}

TestRequestHandler::Response TestRequestHandler::handleGetState(
    ECS::World& world,
    EditorContext& ctx,
    u32 requestId) {
    
    std::ostringstream oss;
    oss << "{\"selected_count\":" << ctx.selectedEntities.size();
    
    ECS::ComponentQuery q;
    u32 entityCount = 0;
    world.forEach(q, [&](ECS::Entity e) {
        if (e.isValid()) entityCount++;
    });
    
    oss << ",\"entity_count\":" << entityCount << "}";
    
    Response resp;
    resp.success = true;
    resp.id = requestId;
    resp.action = "get_state";
    resp.data = oss.str();
    return resp;
}

}
