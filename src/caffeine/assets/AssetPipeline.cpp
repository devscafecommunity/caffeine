#include "assets/AssetPipeline.hpp"
#include "core/io/CafWriter.hpp"

#include <algorithm>
#include <cstdio>
#include <cstring>

namespace Caffeine::Assets {

AssetPipeline::AssetPipeline() = default;
AssetPipeline::~AssetPipeline() = default;

void AssetPipeline::Initialize(const std::filesystem::path& projectRoot) {
    m_ProjectRoot  = std::filesystem::absolute(projectRoot);
    m_RawDir       = m_ProjectRoot / "assets" / "raw";
    m_ProcessedDir = m_ProjectRoot / "assets" / "processed";
    m_ManifestPath = m_ProjectRoot / "assets" / "manifest.caf";

    std::filesystem::create_directories(m_RawDir);
    std::filesystem::create_directories(m_ProcessedDir);

    LoadManifest();
    m_FileWatcher.start(m_RawDir, false);
}

void AssetPipeline::Update() {
    if (!m_FileWatcher.isRunning()) return;

    auto changes = m_FileWatcher.poll();
    for (auto& path : changes) {
        OnFileDetected(path);
    }

    if (!m_ImportQueue.empty() && !m_Busy) {
        ProcessQueue();
    }
}

void AssetPipeline::RegisterCompiler(std::unique_ptr<IAssetCompiler> compiler) {
    auto exts = compiler->GetSupportedExtensions();
    for (const auto& ext : exts) {
        m_Compilers[ext] = compiler.get();
    }
    m_CompilerOwned.push_back(std::move(compiler));
}

void AssetPipeline::Reimport(const std::filesystem::path& path) {
    OnFileDetected(path);
    if (!m_Busy) ProcessQueue();
}

void AssetPipeline::OnFileDetected(const std::filesystem::path& path) {
    if (!std::filesystem::exists(path)) return;
    if (std::filesystem::is_directory(path)) return;
    m_ImportQueue.push(path);
}

void AssetPipeline::ProcessQueue() {
    if (m_ImportQueue.empty()) return;

    m_Busy = true;
    m_CurrentProgress = 0.0f;

    size_t total   = m_ImportQueue.size();
    size_t current = 0;

    while (!m_ImportQueue.empty()) {
        auto sourcePath = m_ImportQueue.front();
        m_ImportQueue.pop();

        std::string ext = sourcePath.extension().string();
        for (auto& c : ext) {
            c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        }

        IAssetCompiler* compiler = FindCompiler(ext);
        if (!compiler) continue;

        std::filesystem::path destName = sourcePath.stem();
        destName += ".caf";
        std::filesystem::path destPath = m_ProcessedDir / destName;

        AssetImportContext ctx;
        ctx.SourcePath      = sourcePath;
        ctx.DestinationPath = destPath;

        bool ok = compiler->Compile(ctx);
        if (ok) {
            ManifestEntry entry;
            entry.type = AssetType::Texture;
            entry.relativePath = std::filesystem::relative(destPath, m_ProjectRoot).generic_string();
            entry.sourcePath   = std::filesystem::relative(sourcePath, m_ProjectRoot).generic_string();

            auto it = std::find_if(m_Manifest.begin(), m_Manifest.end(),
                [&](const ManifestEntry& e) { return e.sourcePath == entry.sourcePath; });
            if (it != m_Manifest.end()) m_Manifest.erase(it);

            m_Manifest.push_back(std::move(entry));
            SaveManifest();
        }

        ++current;
        m_CurrentProgress = static_cast<float>(current) / static_cast<float>(total);
    }

    m_Busy = false;
    m_CurrentProgress = 1.0f;
}

IAssetCompiler* AssetPipeline::FindCompiler(const std::string& extension) {
    auto it = m_Compilers.find(extension);
    if (it != m_Compilers.end()) return it->second;

    std::string lower = extension;
    for (auto& c : lower) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    auto it2 = m_Compilers.find(lower);
    if (it2 != m_Compilers.end()) return it2->second;

    return nullptr;
}

void AssetPipeline::LoadManifest() {
    if (!std::filesystem::exists(m_ManifestPath)) return;

    FILE* f = std::fopen(m_ManifestPath.string().c_str(), "rb");
    if (!f) return;

    CafHeader header;
    if (std::fread(&header, sizeof(header), 1, f) != 1) {
        std::fclose(f);
        return;
    }
    if (header.magic != CafHeader::kMagic) {
        std::fclose(f);
        return;
    }

    std::vector<u8> metaData(header.metadataSize);
    if (header.metadataSize > 0) {
        if (std::fread(metaData.data(), 1, header.metadataSize, f) != header.metadataSize) {
            std::fclose(f);
            return;
        }

        u32 count = 0;
        if (header.metadataSize < sizeof(count)) { std::fclose(f); return; }
        std::memcpy(&count, metaData.data(), sizeof(count));

        u64 offset = sizeof(count);
        for (u32 i = 0; i < count; ++i) {
            if (offset + sizeof(AssetType) + sizeof(u64) > header.metadataSize) break;
            ManifestEntry entry;
            std::memcpy(&entry.type, metaData.data() + offset, sizeof(AssetType));
            offset += sizeof(AssetType);

            u64 pathLen = 0, srcLen = 0;
            std::memcpy(&pathLen, metaData.data() + offset, sizeof(u64));
            offset += sizeof(u64);
            if (offset + pathLen > header.metadataSize) break;
            entry.relativePath.assign(reinterpret_cast<char*>(metaData.data() + offset), pathLen);
            offset += pathLen;

            std::memcpy(&srcLen, metaData.data() + offset, sizeof(u64));
            offset += sizeof(u64);
            if (offset + srcLen > header.metadataSize) break;
            entry.sourcePath.assign(reinterpret_cast<char*>(metaData.data() + offset), srcLen);
            offset += srcLen;

            m_Manifest.push_back(std::move(entry));
        }
    }

    std::fclose(f);
}

void AssetPipeline::SaveManifest() {
    u32 count = static_cast<u32>(m_Manifest.size());
    u64 metadataSize = sizeof(count);

    for (const auto& entry : m_Manifest) {
        metadataSize += sizeof(AssetType);
        metadataSize += sizeof(u64) + entry.relativePath.size();
        metadataSize += sizeof(u64) + entry.sourcePath.size();
    }

    std::vector<u8> metaData(metadataSize);
    u64 offset = 0;
    std::memcpy(metaData.data() + offset, &count, sizeof(count));
    offset += sizeof(count);

    for (const auto& entry : m_Manifest) {
        std::memcpy(metaData.data() + offset, &entry.type, sizeof(AssetType));
        offset += sizeof(AssetType);

        u64 pathLen = entry.relativePath.size();
        std::memcpy(metaData.data() + offset, &pathLen, sizeof(u64));
        offset += sizeof(u64);
        std::memcpy(metaData.data() + offset, entry.relativePath.data(), pathLen);
        offset += pathLen;

        u64 srcLen = entry.sourcePath.size();
        std::memcpy(metaData.data() + offset, &srcLen, sizeof(u64));
        offset += sizeof(u64);
        std::memcpy(metaData.data() + offset, entry.sourcePath.data(), srcLen);
        offset += srcLen;
    }

    IO::CafWriter::write(
        m_ManifestPath.string().c_str(),
        AssetType::Unknown,
        0,
        metaData.data(),
        metadataSize,
        nullptr,
        0
    );
}

} // namespace Caffeine::Assets
