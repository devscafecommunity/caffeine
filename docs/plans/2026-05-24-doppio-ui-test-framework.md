# Doppio UI Test Framework Implementation Plan

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:executing-plans or superpowers:subagent-driven-development to implement this plan task-by-task.

**Goal:** Implement a JSON-based test framework that allows Python to autonomously test Doppio's UI (clicking, multi-selecting, deleting, focusing camera) without screenshots or OCR.

**Architecture:** 
- C++ side: Capture UI state (entity positions, viewport info) and process click commands via JSON protocol
- Python side: Send REQUEST JSON via stdin, parse RESPONSE JSON from stdout, execute assertions
- Protocol: Simple REQUEST/RESPONSE pattern over stdout/stdin with JSON payloads

**Tech Stack:** C++20 (Doppio), Python 3.8+, JSON (via streams), ImGui for coordinate mapping

---

## Task 1: Create TestUIMapper Header (C++)

**Files:**
- Create: `src/editor/TestUIMapper.hpp`

**Objective:** Define data structures and API for capturing viewport state and simulating clicks

**Code:**

```cpp
#pragma once

#include "core/Types.hpp"
#include "ecs/Entity.hpp"
#include "math/Vec3.hpp"
#include <vector>
#include <string>
#include <sstream>

namespace Caffeine::Editor {

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
```

**Steps:**
1. Create file: `cat > /home/pedro/repo/caffeine/src/editor/TestUIMapper.hpp << 'EOF'` (paste code above)
2. Verify: `ls -lh /home/pedro/repo/caffeine/src/editor/TestUIMapper.hpp`

---

## Task 2: Create TestRequestHandler Header (C++)

**Files:**
- Create: `src/editor/TestRequestHandler.hpp`

**Objective:** Define request/response protocol for JSON communication

**Code:**

```cpp
#pragma once

#include "core/Types.hpp"
#include "ecs/Entity.hpp"
#include "ecs/World.hpp"
#include <string>
#include <sstream>

namespace Caffeine::Editor {

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
```

---

## Task 3-8: Implementation and Testing

See inline implementation details. Full test framework requires:

1. TestUIMapper.cpp - Capture viewport state, hit detection
2. TestRequestHandler.cpp - Parse JSON, generate responses
3. SceneViewport.cpp - Integrate handlers into render loop
4. doppio_ui_client.py - Python test automation
5. Compilation and full test run
6. Final commit

---

## Success Criteria

- ✅ All C++ files compile without errors
- ✅ Python client successfully communicates with Doppio
- ✅ Full workflow test passes: click → multi-select → delete → focus
- ✅ Test completes in < 30 seconds
- ✅ All code committed

---
