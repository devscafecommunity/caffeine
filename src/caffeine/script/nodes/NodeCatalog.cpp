#include "script/nodes/NodeCatalog.hpp"

namespace Caffeine::Script::Nodes {

const std::vector<NodeCatalogEntry>& nodeCatalog() {
    static const std::vector<NodeCatalogEntry> entries = {
        {NodeKind::Constant, "Constant", "Constant", "Language / Values",
         "A fixed number or boolean value."},
        {NodeKind::Branch, "Branch", "If / Else", "Language / Flow",
         "Run one exec branch when the condition is true, the other when false."},

        {NodeKind::OnUpdate, "OnUpdate", "On Update", "Engine / Events",
         "Runs once per frame while the entity is in play mode."},
        {NodeKind::SetPosition, "SetPosition", "Set Position", "Engine / Transform",
         "Move the entity to an absolute position."},
        {NodeKind::Translate, "Translate", "Translate", "Engine / Transform",
         "Move the entity by a relative offset."},
        {NodeKind::GetPosition, "GetPosition", "Get Position", "Engine / Transform",
         "Read the entity position."},
        {NodeKind::SetScale, "SetScale", "Set Scale", "Engine / Transform",
         "Set the entity scale on all three axes."},
        {NodeKind::GetScale, "GetScale", "Get Scale", "Engine / Transform",
         "Read the entity scale."},
        {NodeKind::DeltaTime, "DeltaTime", "Delta Time", "Engine / Time",
         "Seconds elapsed since the previous frame."},
        {NodeKind::Log, "Log", "Log", "Engine / Debug",
         "Write a message to the Doppio console."},
    };
    return entries;
}

const NodeCatalogEntry* findNodeCatalogEntry(const std::string& typeName) {
    for (const NodeCatalogEntry& entry : nodeCatalog()) {
        if (typeName == entry.typeName) return &entry;
    }
    return nullptr;
}

} // namespace Caffeine::Script::Nodes
