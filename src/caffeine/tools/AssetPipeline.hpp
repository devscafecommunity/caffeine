#pragma once

#include "tools/PipelineTypes.hpp"
#include "tools/TextureEncoder.hpp"
#include "tools/AudioEncoder.hpp"
#include "tools/MeshEncoder.hpp"
#include "tools/AssetManifest.hpp"
#include <filesystem>
#include <chrono>

namespace Caffeine::Tools {
using namespace Caffeine;

class AssetPipeline {
public:
    struct BatchOptions {
        std::string inputDir;
        std::string outputDir;
        std::string manifestPath;
        bool forceRebuild = false;
        bool verbose      = false;
        u32  threadCount  = 4;
    };

    struct BatchReport {
        u32 converted = 0;
        u32 skipped   = 0;
        u32 errors    = 0;
        std::vector<std::string> errorMessages;
        f64 totalTimeSeconds = 0.0;
        u64 totalInputBytes  = 0;
        u64 totalOutputBytes = 0;
    };

    BatchReport run(const BatchOptions& opts) {
        BatchReport report;
        
        auto startTime = std::chrono::steady_clock::now();
        
        try {
            if (!std::filesystem::exists(opts.inputDir)) {
                report.errors = 1;
                report.errorMessages.push_back("Input directory does not exist: " + opts.inputDir);
                return report;
            }
            
            AssetManifest manifest;
            
            for (const auto& entry : std::filesystem::recursive_directory_iterator(opts.inputDir)) {
                if (!entry.is_regular_file()) continue;
                
                AssetType type = detectAssetType(entry.path());
                if (type == AssetType::Unknown) continue;
                
                std::filesystem::path relativePath = std::filesystem::relative(entry.path(), opts.inputDir);
                std::filesystem::path outputPath = std::filesystem::path(opts.outputDir) / relativePath;
                outputPath.replace_extension(".caf");
                
                if (!opts.forceRebuild && !isOutdated(entry.path(), outputPath)) {
                    ++report.skipped;
                    continue;
                }
                
                try {
                    std::filesystem::create_directories(outputPath.parent_path());
                } catch (...) {
                    ++report.errors;
                    report.errorMessages.push_back("Failed to create directory: " + outputPath.parent_path().string());
                    continue;
                }
                
                ConversionResult result;
                
                switch (type) {
                    case AssetType::Texture:
                        result = m_textureEncoder.encode(entry.path().string(), outputPath.string());
                        break;
                    case AssetType::Audio:
                        result = m_audioEncoder.encode(entry.path().string(), outputPath.string());
                        break;
                    case AssetType::Mesh:
                        result = m_meshEncoder.encode(entry.path().string(), outputPath.string());
                        break;
                    default:
                        result.errorMessage = "Unsupported asset type";
                        break;
                }
                
                if (result.success) {
                    ++report.converted;
                    report.totalInputBytes += result.inputBytes;
                    report.totalOutputBytes += result.outputBytes;
                    
                    if (!opts.manifestPath.empty()) {
                        AssetManifestEntry manifestEntry;
                        manifestEntry.id = relativePath.stem().string();
                        manifestEntry.path = relativePath.string();
                        manifestEntry.type = type;
                        manifestEntry.sizeBytes = result.outputBytes;
                        manifestEntry.crc32 = 0;
                        manifest.addEntry(manifestEntry);
                    }
                } else {
                    ++report.errors;
                    report.errorMessages.push_back(entry.path().string() + ": " + result.errorMessage);
                }
            }
            
            if (!opts.manifestPath.empty() && manifest.entryCount() > 0) {
                if (!manifest.save(opts.manifestPath)) {
                    report.errorMessages.push_back("Failed to save manifest: " + opts.manifestPath);
                }
            }
            
        } catch (const std::exception& e) {
            ++report.errors;
            report.errorMessages.push_back(std::string("Exception: ") + e.what());
        } catch (...) {
            ++report.errors;
            report.errorMessages.push_back("Unknown exception during pipeline execution");
        }
        
        auto endTime = std::chrono::steady_clock::now();
        std::chrono::duration<f64> elapsed = endTime - startTime;
        report.totalTimeSeconds = elapsed.count();
        
        return report;
    }

    AssetType detectAssetType(const std::filesystem::path& path) const {
        std::string ext = path.extension().string();
        
        if (ext == ".png" || ext == ".bmp" || ext == ".tga" || 
            ext == ".jpg" || ext == ".jpeg") {
            return AssetType::Texture;
        }
        
        if (ext == ".wav" || ext == ".ogg" || ext == ".mp3") {
            return AssetType::Audio;
        }
        
        if (ext == ".obj" || ext == ".gltf" || ext == ".glb") {
            return AssetType::Mesh;
        }
        
        if (ext == ".glsl" || ext == ".hlsl" || ext == ".spv") {
            return AssetType::Shader;
        }
        
        return AssetType::Unknown;
    }
    
    bool isOutdated(const std::filesystem::path& src,
                    const std::filesystem::path& dst) const {
        try {
            if (!std::filesystem::exists(dst)) {
                return true;
            }
            
            auto srcTime = std::filesystem::last_write_time(src);
            auto dstTime = std::filesystem::last_write_time(dst);
            
            return srcTime > dstTime;
        } catch (...) {
            return true;
        }
    }

private:
    TextureEncoder m_textureEncoder;
    AudioEncoder   m_audioEncoder;
    MeshEncoder    m_meshEncoder;
};

} // namespace Caffeine::Tools
