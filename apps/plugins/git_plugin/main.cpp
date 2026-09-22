#include "caffeine/plugin/PluginAPI.hpp"
#include "editor/EditorContext.hpp"
#include "GitRunner.hpp"

#ifdef CF_HAS_IMGUI
#include <imgui.h>
#endif

#include <algorithm>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

namespace {

class GitPlugin : public Caffeine::Editor::IPlugin {
public:
    explicit GitPlugin(const Caffeine::Editor::PluginHostApi* host) : m_host(host) {}

    void OnLoad() override {
        if (!m_host || !m_host->registerPanel) return;
        m_host->registerPanel(m_host->editorContext, GetName(), "Git",
                              &GitPlugin::renderPanel, this);
        if (m_host->registerMenuAction) {
            m_host->registerMenuAction(m_host->editorContext, GetName(), "Version Control/Git Panel",
                                       &GitPlugin::openPanelMenu, this);
        }
    }

    void OnUnload() override {}
    void OnUpdate(float) override {}

    const char* GetName() const override { return "Git"; }
    const char* GetVersion() const override { return "0.1.0"; }
    const char* GetDescription() const override {
        return "Optional Git project management — status, stage, commit, branches, remotes.";
    }

private:
    static Caffeine::Editor::EditorContext* editorCtx(const Caffeine::Editor::PluginHostApi* host) {
        return host ? static_cast<Caffeine::Editor::EditorContext*>(host->editorContext) : nullptr;
    }

    static void openPanelMenu(void* userData) {
        auto* self = static_cast<GitPlugin*>(userData);
        if (self->m_host && self->m_host->openPanel) {
            self->m_host->openPanel(self->m_host->editorContext, "Git");
        }
    }

    std::filesystem::path projectRoot() const {
        if (!m_host || !m_host->getProjectRootPath) return {};
        char buffer[1024] = {};
        if (!m_host->getProjectRootPath(m_host->editorContext, buffer, sizeof(buffer))) {
            return {};
        }
        return std::filesystem::path(buffer);
    }

    void refresh() {
        m_repoRoot = projectRoot();
        m_branch = Caffeine::Plugins::Git::currentBranch(m_repoRoot);
        m_files = Caffeine::Plugins::Git::listStatus(m_repoRoot);
        m_branches = Caffeine::Plugins::Git::listBranches(m_repoRoot);
        m_remotes = Caffeine::Plugins::Git::listRemotes(m_repoRoot);
        m_log = Caffeine::Plugins::Git::listLog(m_repoRoot, 40);
        m_lastOutput.clear();
    }

    void runGitCommand(const std::vector<std::string>& args) {
        const auto result = Caffeine::Plugins::Git::runGit(m_repoRoot, args);
        m_lastOutput = result.output;
        if (result.exitCode != 0 && m_host->logError) {
            m_host->logError(m_host->editorContext, result.output.c_str());
        } else if (m_host->logInfo) {
            m_host->logInfo(m_host->editorContext, "Git command completed.");
        }
        refresh();
    }

    static void renderPanel(void* userData) {
#ifdef CF_HAS_IMGUI
        auto* self = static_cast<GitPlugin*>(userData);
        if (self->m_needsRefresh) {
            self->refresh();
            self->m_needsRefresh = false;
        }

        ImGui::TextWrapped(
            "Optional Git panel — not part of Doppio core. Requires git in PATH and an open project.");

        if (ImGui::Button("Refresh")) self->refresh();

        if (self->m_repoRoot.empty()) {
            ImGui::TextDisabled("Open a project scene to detect repository root.");
            return;
        }

        if (!Caffeine::Plugins::Git::isGitRepository(self->m_repoRoot)) {
            ImGui::TextColored(ImVec4(1, 0.5f, 0.3f, 1), "Not a git repository: %s",
                               self->m_repoRoot.string().c_str());
            if (ImGui::Button("git init")) {
                self->runGitCommand({"init"});
            }
            return;
        }

        ImGui::Text("Repo: %s", self->m_repoRoot.string().c_str());
        ImGui::Text("Branch: %s", self->m_branch.empty() ? "(detached)" : self->m_branch.c_str());

        if (ImGui::CollapsingHeader("Changes", ImGuiTreeNodeFlags_DefaultOpen)) {
            if (ImGui::Button("Stage all")) self->runGitCommand({"add", "-A"});
            ImGui::SameLine();
            if (ImGui::Button("Unstage all")) self->runGitCommand({"reset"});

            ImGui::BeginChild("git_changes", ImVec2(0, 160), true);
            for (std::size_t i = 0; i < self->m_files.size(); ++i) {
                const auto& file = self->m_files[i];
                ImGui::PushID(static_cast<int>(i));
                ImGui::Text("%c%c %s", file.index, file.workTree, file.path.c_str());
                ImGui::SameLine();
                if (ImGui::SmallButton("Stage")) {
                    self->runGitCommand({"add", file.path});
                }
                ImGui::SameLine();
                if (ImGui::SmallButton("Diff")) {
                    self->m_lastOutput =
                        Caffeine::Plugins::Git::runGit(self->m_repoRoot, {"diff", file.path}).output;
                }
                ImGui::PopID();
            }
            ImGui::EndChild();
        }

        if (ImGui::CollapsingHeader("Commit", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::InputTextMultiline("Message", self->m_commitMessage, sizeof(self->m_commitMessage),
                                      ImVec2(-1, 60));
            if (ImGui::Button("Commit") && self->m_commitMessage[0] != '\0') {
                self->runGitCommand({"commit", "-m", self->m_commitMessage});
                self->m_commitMessage[0] = '\0';
            }
        }

        if (ImGui::CollapsingHeader("Branches")) {
            int branchIndex = 0;
            for (std::size_t i = 0; i < self->m_branches.size(); ++i) {
                if (self->m_branches[i] == self->m_branch) branchIndex = static_cast<int>(i);
            }
            if (!self->m_branches.empty()) {
                std::vector<const char*> labels;
                labels.reserve(self->m_branches.size());
                for (const auto& b : self->m_branches) labels.push_back(b.c_str());
                if (ImGui::Combo("Checkout", &branchIndex, labels.data(),
                                 static_cast<int>(labels.size()))) {
                    self->runGitCommand({"checkout", self->m_branches[branchIndex]});
                }
            }
            ImGui::InputText("New branch", self->m_newBranch, sizeof(self->m_newBranch));
            ImGui::SameLine();
            if (ImGui::Button("Create") && self->m_newBranch[0] != '\0') {
                self->runGitCommand({"checkout", "-b", self->m_newBranch});
                self->m_newBranch[0] = '\0';
            }
        }

        if (ImGui::CollapsingHeader("Remotes & sync")) {
            for (const auto& remote : self->m_remotes) {
                ImGui::BulletText("%s", remote.c_str());
            }
            if (ImGui::Button("Fetch")) self->runGitCommand({"fetch", "--all", "--prune"});
            ImGui::SameLine();
            if (ImGui::Button("Pull")) self->runGitCommand({"pull"});
            ImGui::SameLine();
            if (ImGui::Button("Push")) self->runGitCommand({"push"});
        }

        if (ImGui::CollapsingHeader("History")) {
            ImGui::BeginChild("git_log", ImVec2(0, 120), true);
            for (const auto& entry : self->m_log) {
                if (ImGui::Selectable(entry.c_str())) {
                    self->m_lastOutput = entry;
                }
            }
            ImGui::EndChild();
        }

        if (ImGui::CollapsingHeader("Stash")) {
            if (ImGui::Button("Stash push")) self->runGitCommand({"stash", "push", "-u"});
            ImGui::SameLine();
            if (ImGui::Button("Stash pop")) self->runGitCommand({"stash", "pop"});
            ImGui::SameLine();
            if (ImGui::Button("Stash list")) {
                self->m_lastOutput =
                    Caffeine::Plugins::Git::runGit(self->m_repoRoot, {"stash", "list"}).output;
            }
        }

        if (!self->m_lastOutput.empty()) {
            ImGui::Separator();
            ImGui::TextUnformatted("Output");
            ImGui::BeginChild("git_output", ImVec2(0, 120), true);
            ImGui::TextUnformatted(self->m_lastOutput.c_str());
            ImGui::EndChild();
        }
#else
        (void)userData;
#endif
    }

    const Caffeine::Editor::PluginHostApi* m_host = nullptr;
    std::filesystem::path m_repoRoot;
    std::string m_branch;
    std::vector<Caffeine::Plugins::Git::GitFileStatus> m_files;
    std::vector<std::string> m_branches;
    std::vector<std::string> m_remotes;
    std::vector<std::string> m_log;
    std::string m_lastOutput;
    char m_commitMessage[512] = {};
    char m_newBranch[128] = {};
    bool m_needsRefresh = true;
};

GitPlugin* g_plugin = nullptr;

}  // namespace

extern "C" {

CAFFEINE_PLUGIN_API Caffeine::Editor::IPlugin* CreatePlugin(
    const Caffeine::Editor::PluginHostApi* host) {
    g_plugin = new GitPlugin(host);
    return g_plugin;
}

CAFFEINE_PLUGIN_API void DestroyPlugin(Caffeine::Editor::IPlugin* plugin) {
    delete plugin;
    g_plugin = nullptr;
}

}  // extern "C"
