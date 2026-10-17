#pragma once

#include "core/Types.hpp"

#include <vector>

namespace Caffeine::Render {

struct InstanceBatchItem {
    u64 key = 0;
    bool allow = false;
};

struct InstanceBatchGroup {
    std::vector<u32> members;
};

/// Groups items that share `key`. Items with allow=false stay as single draws.
/// Groups are split at `maxPerBatch`.
inline std::vector<InstanceBatchGroup> buildInstanceBatches(const std::vector<InstanceBatchItem>& items,
                                                            u32 maxPerBatch) {
    std::vector<InstanceBatchGroup> groups;
    const u32 limit = maxPerBatch < 2 ? 2 : maxPerBatch;
    for (u32 i = 0; i < items.size(); ++i) {
        if (!items[i].allow || items[i].key == 0) continue;
        InstanceBatchGroup* found = nullptr;
        for (InstanceBatchGroup& group : groups) {
            if (group.members.empty()) continue;
            const u32 head = group.members.front();
            if (items[head].key == items[i].key && group.members.size() < limit) {
                found = &group;
                break;
            }
        }
        if (!found) {
            groups.push_back({});
            found = &groups.back();
        }
        found->members.push_back(i);
    }
    return groups;
}

}  // namespace Caffeine::Render
