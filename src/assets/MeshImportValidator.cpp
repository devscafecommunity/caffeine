#include "assets/MeshImportValidator.hpp"
#include <algorithm>
#include <cctype>
#include <fstream>
#include <sstream>

namespace Caffeine::Assets {

namespace {

std::string extensionLower(const std::filesystem::path& path) {
    std::string ext = path.extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return ext;
}

void appendUniquePath(std::vector<std::filesystem::path>& out,
                      const std::filesystem::path& candidate) {
    if (candidate.empty()) return;
    for (const auto& existing : out) {
        std::error_code ec;
        if (std::filesystem::equivalent(existing, candidate, ec) && !ec) {
            return;
        }
        if (existing == candidate) return;
    }
    out.push_back(candidate);
}

std::vector<std::string> listObjExternalFiles(const std::filesystem::path& objPath) {
    std::vector<std::string> deps;
    std::ifstream in(objPath);
    if (!in) return deps;

    const std::filesystem::path dir = objPath.parent_path();
    std::string line;
    while (std::getline(in, line)) {
        if (line.rfind("mtllib ", 0) == 0) {
            std::string mtlName = line.substr(7);
            while (!mtlName.empty() && std::isspace(static_cast<unsigned char>(mtlName.front()))) {
                mtlName.erase(mtlName.begin());
            }
            while (!mtlName.empty() && std::isspace(static_cast<unsigned char>(mtlName.back()))) {
                mtlName.pop_back();
            }
            if (!mtlName.empty()) {
                deps.push_back(mtlName);
            }
        }
    }

    for (const std::string& mtlName : deps) {
        const std::filesystem::path mtlPath = dir / mtlName;
        std::ifstream mtl(mtlPath);
        if (!mtl) continue;

        std::string mtlLine;
        while (std::getline(mtl, mtlLine)) {
            if (mtlLine.rfind("map_", 0) != 0) continue;
            const auto space = mtlLine.find(' ');
            if (space == std::string::npos) continue;
            std::string texName = mtlLine.substr(space + 1);
            while (!texName.empty() && std::isspace(static_cast<unsigned char>(texName.front()))) {
                texName.erase(texName.begin());
            }
            while (!texName.empty() && std::isspace(static_cast<unsigned char>(texName.back()))) {
                texName.pop_back();
            }
            if (!texName.empty()) {
                deps.push_back(texName);
            }
        }
    }

    std::sort(deps.begin(), deps.end());
    deps.erase(std::unique(deps.begin(), deps.end()), deps.end());
    return deps;
}

void classifyDependencies(const std::filesystem::path& meshDir,
                          const std::vector<std::string>& uris,
                          MeshImportReport& report) {
    report.dependencies = uris;
    for (const std::string& uri : uris) {
        const std::filesystem::path dep = meshDir / uri;
        if (std::filesystem::exists(dep)) {
            report.presentDependencies.push_back(uri);
        } else {
            report.missingDependencies.push_back(uri);
        }
    }
}

}  // namespace

std::vector<std::string> MeshImportValidator::listGltfExternalUris(
    const std::filesystem::path& gltfPath) {
    std::vector<std::string> uris;
    std::ifstream in(gltfPath);
    if (!in) return uris;

    std::stringstream buffer;
    buffer << in.rdbuf();
    const std::string json = buffer.str();

    size_t pos = 0;
    while ((pos = json.find("\"uri\"", pos)) != std::string::npos) {
        pos = json.find(':', pos);
        if (pos == std::string::npos) break;
        pos = json.find('"', pos);
        if (pos == std::string::npos) break;
        const size_t start = pos + 1;
        const size_t end = json.find('"', start);
        if (end == std::string::npos) break;

        const std::string uri = json.substr(start, end - start);
        pos = end + 1;

        if (uri.rfind("data:", 0) == 0) continue;
        if (std::find(uris.begin(), uris.end(), uri) == uris.end()) {
            uris.push_back(uri);
        }
    }

    return uris;
}

MeshImportReport MeshImportValidator::analyze(const std::filesystem::path& meshPath) {
    MeshImportReport report;
    report.meshFileExists = std::filesystem::exists(meshPath);
    if (!report.meshFileExists) {
        report.errorSummary = "Ficheiro nao encontrado";
        report.suggestion = "Verifique o caminho do mesh no Inspector.";
        return report;
    }

    const std::string ext = extensionLower(meshPath);
    const std::filesystem::path meshDir = meshPath.parent_path();

    if (ext == ".glb") {
        report.format = "glb";
        report.readyToLoad = true;
        return report;
    }

    if (ext == ".gltf") {
        report.format = "gltf";
        classifyDependencies(meshDir, listGltfExternalUris(meshPath), report);
        report.readyToLoad = report.missingDependencies.empty();
        if (!report.readyToLoad) {
            report.errorSummary = "Ficheiros em falta: ";
            for (size_t i = 0; i < report.missingDependencies.size(); ++i) {
                if (i > 0) report.errorSummary += ", ";
                report.errorSummary += report.missingDependencies[i];
            }
            report.suggestion =
                "Copie o .bin e as texturas para assets/raw, ou exporte como .glb (tudo num ficheiro).";
        }
        return report;
    }

    if (ext == ".obj") {
        report.format = "obj";
        classifyDependencies(meshDir, listObjExternalFiles(meshPath), report);
        report.readyToLoad = report.missingDependencies.empty();
        if (!report.readyToLoad) {
            report.errorSummary = "Ficheiros em falta: ";
            for (size_t i = 0; i < report.missingDependencies.size(); ++i) {
                if (i > 0) report.errorSummary += ", ";
                report.errorSummary += report.missingDependencies[i];
            }
            report.suggestion = "Importe o .obj com o .mtl e texturas na mesma pasta.";
        } else {
            report.readyToLoad = true;
        }
        return report;
    }

    if (ext == ".fbx") {
        report.format = "fbx";
        const std::filesystem::path objPath = meshPath.parent_path() / (meshPath.stem().string() + ".obj");
        if (std::filesystem::exists(objPath)) {
            return analyze(objPath);
        }
        report.readyToLoad = false;
        report.errorSummary = "FBX nao suportado diretamente";
        report.suggestion = "Exporte como OBJ ou glTF (.glb recomendado).";
        return report;
    }

    report.format = ext.empty() ? "unknown" : ext.substr(1);
    report.readyToLoad = false;
    report.errorSummary = "Formato de mesh nao suportado";
    report.suggestion = "Use OBJ, glTF ou GLB.";
    return report;
}

std::vector<std::filesystem::path> MeshImportValidator::collectImportBundle(
    const std::filesystem::path& sourceMeshPath) {
    std::vector<std::filesystem::path> bundle;
    appendUniquePath(bundle, sourceMeshPath);

    const std::string ext = extensionLower(sourceMeshPath);
    const std::filesystem::path sourceDir = sourceMeshPath.parent_path();

    if (ext == ".gltf") {
        for (const std::string& uri : listGltfExternalUris(sourceMeshPath)) {
            const std::filesystem::path dep = sourceDir / uri;
            if (std::filesystem::exists(dep)) {
                appendUniquePath(bundle, dep);
            }
        }
    } else if (ext == ".obj") {
        for (const std::string& depName : listObjExternalFiles(sourceMeshPath)) {
            const std::filesystem::path dep = sourceDir / depName;
            if (std::filesystem::exists(dep)) {
                appendUniquePath(bundle, dep);
            }
        }
    }

    return bundle;
}

}  // namespace Caffeine::Assets
