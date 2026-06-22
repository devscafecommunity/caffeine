#pragma once

#include "core/Types.hpp"
#include "ecs/Entity.hpp"
#include "math/Vec3.hpp"
#include <string>
#include <vector>
#include <iostream>
#include <sstream>
#include <cstdlib>

namespace Caffeine::Editor {

class TestInstrumentation {
public:
    static bool isTestMode() {
        const char* val = std::getenv("DOPPIO_TEST_MODE");
        return val && std::string(val) == "1";
    }
    
    static bool isHeadless() {
        const char* val = std::getenv("DOPPIO_HEADLESS");
        return val && std::string(val) == "1";
    }
    
    static void logTestResult(const std::string& key, const std::string& data) {
        if (!isTestMode()) return;
        std::cout << "TEST_RESULT: {\"key\":\"" << key << "\"," << data << "}" << std::endl;
    }
    
    static void onEntitySelected(ECS::Entity entity) {
        if (!isTestMode()) return;
        std::ostringstream oss;
        oss << "\"id\":" << entity.id();
        logTestResult("selected_entity", oss.str());
    }
    
    static void onEntitiesSelected(const std::vector<ECS::Entity>& entities) {
        if (!isTestMode()) return;
        std::ostringstream oss;
        oss << "\"ids\":[";
        for (size_t i = 0; i < entities.size(); ++i) {
            if (i > 0) oss << ",";
            oss << entities[i].id();
        }
        oss << "]";
        logTestResult("selected_entities", oss.str());
    }
    
    static void onSceneEntities(const std::vector<ECS::Entity>& entities) {
        if (!isTestMode()) return;
        std::ostringstream oss;
        oss << "\"ids\":[";
        for (size_t i = 0; i < entities.size(); ++i) {
            if (i > 0) oss << ",";
            oss << entities[i].id();
        }
        oss << "]";
        logTestResult("scene_entities", oss.str());
    }
    
    static void onCameraFocused(const Vec3& pos, f32 distance) {
        if (!isTestMode()) return;
        std::ostringstream oss;
        oss << "\"success\":true,\"position\":{\"x\":" << pos.x << ",\"y\":" << pos.y 
            << ",\"z\":" << pos.z << "},\"distance\":" << distance;
        logTestResult("camera_state", oss.str());
    }
    
    static void onSceneLoaded(const std::string& path) {
        if (!isTestMode()) return;
        std::cout << "Scene loaded: " << path << std::endl;
    }
};

} // namespace Caffeine::Editor
