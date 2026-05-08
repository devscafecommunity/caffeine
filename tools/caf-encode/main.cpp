#include "Caffeine.hpp"
#include "tools/TextureEncoder.hpp"
#include "tools/AudioEncoder.hpp"
#include "tools/MeshEncoder.hpp"
#include "tools/AssetPipeline.hpp"
#include <cstdio>
#include <cstring>
#include <string>

using namespace Caffeine;
using namespace Caffeine::Tools;

static void printUsage() {
    std::printf("caf-encode - Caffeine Asset Pipeline Tool\n\n");
    std::printf("Usage:\n");
    std::printf("  caf-encode texture <input> <output> [options]\n");
    std::printf("  caf-encode audio <input> <output> [options]\n");
    std::printf("  caf-encode mesh <input> <output> [options]\n");
    std::printf("  caf-encode batch <input-dir> <output-dir> [options]\n\n");
    std::printf("Texture Options:\n");
    std::printf("  --mipmaps        Generate mipmap chain\n");
    std::printf("  --no-mipmaps     Disable mipmap generation\n\n");
    std::printf("Batch Options:\n");
    std::printf("  --manifest <path>     Save asset manifest\n");
    std::printf("  --force-rebuild       Rebuild all assets\n");
    std::printf("  --jobs <N>            Number of parallel jobs (default: 4)\n");
    std::printf("  --verbose             Print detailed output\n\n");
}

int main(int argc, char** argv) {
    if (argc < 2) {
        printUsage();
        return 1;
    }
    
    std::string command = argv[1];
    
    if (command == "--help" || command == "-h") {
        printUsage();
        return 0;
    }
    
    if (command == "texture") {
        if (argc < 4) {
            std::printf("Error: texture command requires <input> and <output> arguments\n");
            return 1;
        }
        
        std::string inputPath = argv[2];
        std::string outputPath = argv[3];
        
        TextureEncodeOptions opts;
        
        for (int i = 4; i < argc; ++i) {
            if (std::strcmp(argv[i], "--no-mipmaps") == 0) {
                opts.generateMipmaps = false;
            }
            else if (std::strcmp(argv[i], "--mipmaps") == 0) {
                opts.generateMipmaps = true;
            }
        }
        
        TextureEncoder encoder;
        auto result = encoder.encode(inputPath, outputPath, opts);
        
        if (result.success) {
            std::printf("Texture encoded: %s -> %s\n", inputPath.c_str(), outputPath.c_str());
            std::printf("  Input:  %llu bytes\n", static_cast<unsigned long long>(result.inputBytes));
            std::printf("  Output: %llu bytes\n", static_cast<unsigned long long>(result.outputBytes));
            std::printf("  Ratio:  %.2f\n", result.compressionRatio);
            return 0;
        } else {
            std::printf("Error: %s\n", result.errorMessage.c_str());
            return 1;
        }
    }
    
    else if (command == "audio") {
        if (argc < 4) {
            std::printf("Error: audio command requires <input> and <output> arguments\n");
            return 1;
        }
        
        std::string inputPath = argv[2];
        std::string outputPath = argv[3];
        
        AudioEncodeOptions opts;
        
        AudioEncoder encoder;
        auto result = encoder.encode(inputPath, outputPath, opts);
        
        if (result.success) {
            std::printf("Audio encoded: %s -> %s\n", inputPath.c_str(), outputPath.c_str());
            std::printf("  Input:  %llu bytes\n", static_cast<unsigned long long>(result.inputBytes));
            std::printf("  Output: %llu bytes\n", static_cast<unsigned long long>(result.outputBytes));
            std::printf("  Ratio:  %.2f\n", result.compressionRatio);
            return 0;
        } else {
            std::printf("Error: %s\n", result.errorMessage.c_str());
            return 1;
        }
    }
    
    else if (command == "mesh") {
        if (argc < 4) {
            std::printf("Error: mesh command requires <input> and <output> arguments\n");
            return 1;
        }
        
        std::string inputPath = argv[2];
        std::string outputPath = argv[3];
        
        MeshEncodeOptions opts;
        
        MeshEncoder encoder;
        auto result = encoder.encode(inputPath, outputPath, opts);
        
        if (result.success) {
            std::printf("Mesh encoded: %s -> %s\n", inputPath.c_str(), outputPath.c_str());
            std::printf("  Input:  %llu bytes\n", static_cast<unsigned long long>(result.inputBytes));
            std::printf("  Output: %llu bytes\n", static_cast<unsigned long long>(result.outputBytes));
            std::printf("  Ratio:  %.2f\n", result.compressionRatio);
            return 0;
        } else {
            std::printf("Error: %s\n", result.errorMessage.c_str());
            return 1;
        }
    }
    
    else if (command == "batch") {
        if (argc < 4) {
            std::printf("Error: batch command requires <input-dir> and <output-dir> arguments\n");
            return 1;
        }
        
        AssetPipeline::BatchOptions opts;
        opts.inputDir = argv[2];
        opts.outputDir = argv[3];
        
        for (int i = 4; i < argc; ++i) {
            if (std::strcmp(argv[i], "--manifest") == 0 && i + 1 < argc) {
                opts.manifestPath = argv[++i];
            }
            else if (std::strcmp(argv[i], "--force-rebuild") == 0) {
                opts.forceRebuild = true;
            }
            else if (std::strcmp(argv[i], "--jobs") == 0 && i + 1 < argc) {
                opts.threadCount = static_cast<u32>(std::atoi(argv[++i]));
            }
            else if (std::strcmp(argv[i], "--verbose") == 0) {
                opts.verbose = true;
            }
        }
        
        AssetPipeline pipeline;
        auto report = pipeline.run(opts);
        
        std::printf("Batch conversion complete:\n");
        std::printf("  Converted: %u\n", report.converted);
        std::printf("  Skipped:   %u\n", report.skipped);
        std::printf("  Errors:    %u\n", report.errors);
        std::printf("  Time:      %.2f seconds\n", report.totalTimeSeconds);
        std::printf("  Input:     %llu bytes\n", static_cast<unsigned long long>(report.totalInputBytes));
        std::printf("  Output:    %llu bytes\n", static_cast<unsigned long long>(report.totalOutputBytes));
        
        if (!report.errorMessages.empty()) {
            std::printf("\nErrors:\n");
            for (const auto& msg : report.errorMessages) {
                std::printf("  - %s\n", msg.c_str());
            }
        }
        
        return report.errors > 0 ? 1 : 0;
    }
    
    else {
        std::printf("Error: Unknown command '%s'\n\n", command.c_str());
        printUsage();
        return 1;
    }
}
