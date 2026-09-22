#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace Caffeine::Plugins::Git {

struct GitResult {
    int exitCode = -1;
    std::string output;
};

struct GitFileStatus {
    char index = ' ';
    char workTree = ' ';
    std::string path;
    bool staged = false;
};

GitResult runGit(const std::filesystem::path& repoRoot, const std::vector<std::string>& args);

bool isGitRepository(const std::filesystem::path& repoRoot);
std::string currentBranch(const std::filesystem::path& repoRoot);
std::vector<GitFileStatus> listStatus(const std::filesystem::path& repoRoot);
std::vector<std::string> listBranches(const std::filesystem::path& repoRoot);
std::vector<std::string> listRemotes(const std::filesystem::path& repoRoot);
std::vector<std::string> listLog(const std::filesystem::path& repoRoot, int maxEntries = 30);

}  // namespace Caffeine::Plugins::Git
