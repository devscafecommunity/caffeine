#include "GitRunner.hpp"

#include <array>
#include <cstdio>
#include <memory>
#include <sstream>

namespace Caffeine::Plugins::Git {
namespace {

std::string shellQuote(const std::string& value) {
    std::string out = "'";
    for (char c : value) {
        if (c == '\'') out += "'\\''";
        else out += c;
    }
    out += "'";
    return out;
}

std::string joinArgs(const std::vector<std::string>& args) {
    std::ostringstream oss;
    for (std::size_t i = 0; i < args.size(); ++i) {
        if (i > 0) oss << ' ';
        oss << shellQuote(args[i]);
    }
    return oss.str();
}

}  // namespace

GitResult runGit(const std::filesystem::path& repoRoot, const std::vector<std::string>& args) {
    GitResult result;
    if (repoRoot.empty()) {
        result.output = "Repository path is empty.";
        return result;
    }

    const std::string cmd =
        "cd " + shellQuote(repoRoot.string()) + " && git " + joinArgs(args) + " 2>&1";
    std::array<char, 256> buffer {};
    FILE* pipe = popen(cmd.c_str(), "r");
    if (!pipe) {
        result.output = "Failed to run git.";
        return result;
    }

    while (fgets(buffer.data(), static_cast<int>(buffer.size()), pipe)) {
        result.output += buffer.data();
    }
    result.exitCode = pclose(pipe);
    if (result.exitCode != 0) {
        result.exitCode = 1;
    }
    return result;
}

bool isGitRepository(const std::filesystem::path& repoRoot) {
    return std::filesystem::exists(repoRoot / ".git");
}

std::string currentBranch(const std::filesystem::path& repoRoot) {
    const GitResult result = runGit(repoRoot, {"branch", "--show-current"});
    if (result.exitCode != 0) return {};
    std::string branch = result.output;
    while (!branch.empty() && (branch.back() == '\n' || branch.back() == '\r')) {
        branch.pop_back();
    }
    return branch;
}

std::vector<GitFileStatus> listStatus(const std::filesystem::path& repoRoot) {
    std::vector<GitFileStatus> files;
    const GitResult result = runGit(repoRoot, {"status", "--porcelain"});
    if (result.exitCode != 0) return files;

    std::istringstream stream(result.output);
    std::string line;
    while (std::getline(stream, line)) {
        if (line.size() < 4) continue;
        GitFileStatus entry;
        entry.index = line[0];
        entry.workTree = line[1];
        entry.path = line.substr(3);
        entry.staged = entry.index != ' ' && entry.index != '?';
        files.push_back(std::move(entry));
    }
    return files;
}

std::vector<std::string> listBranches(const std::filesystem::path& repoRoot) {
    std::vector<std::string> branches;
    const GitResult result = runGit(repoRoot, {"branch", "--format=%(refname:short)"});
    if (result.exitCode != 0) return branches;

    std::istringstream stream(result.output);
    std::string line;
    while (std::getline(stream, line)) {
        if (!line.empty()) branches.push_back(line);
    }
    return branches;
}

std::vector<std::string> listRemotes(const std::filesystem::path& repoRoot) {
    std::vector<std::string> remotes;
    const GitResult result = runGit(repoRoot, {"remote"});
    if (result.exitCode != 0) return remotes;

    std::istringstream stream(result.output);
    std::string line;
    while (std::getline(stream, line)) {
        if (!line.empty()) remotes.push_back(line);
    }
    return remotes;
}

std::vector<std::string> listLog(const std::filesystem::path& repoRoot, int maxEntries) {
    std::vector<std::string> entries;
    const GitResult result =
        runGit(repoRoot, {"log", "--oneline", "-n", std::to_string(maxEntries)});
    if (result.exitCode != 0) return entries;

    std::istringstream stream(result.output);
    std::string line;
    while (std::getline(stream, line)) {
        if (!line.empty()) entries.push_back(line);
    }
    return entries;
}

}  // namespace Caffeine::Plugins::Git
