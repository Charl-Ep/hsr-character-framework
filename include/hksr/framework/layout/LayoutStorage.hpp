#pragma once
#include <hksr/framework/layout/SceneLayout.hpp>
#include <filesystem>

namespace hksr::framework {
// Single worker owner. All file operations stay off the official game frame.
// Named snapshots are explicit; current.json is the automatic working state.
class LayoutStorage {
  public:
    explicit LayoutStorage(std::filesystem::path root) : root_(std::move(root)) {}
    void SaveCurrent(const SceneLayout&);
    std::optional<SceneLayout> Current(const std::string& scene) const;
    void SaveNamed(const std::string& name, const SceneLayout&, bool overwrite = false);
    SceneLayout Named(const std::string& scene, const std::string& name) const;
    std::vector<std::string> Names(const std::string& scene) const;
    void DeleteNamed(const std::string& scene, const std::string& name);

  private:
    std::filesystem::path Directory(const std::string&) const;
    std::filesystem::path NamedPath(const std::string&, const std::string&) const;
    static void Write(const std::filesystem::path&, const SceneLayout&, bool overwrite);
    static SceneLayout Read(const std::filesystem::path&, const std::string& scene);
    std::filesystem::path root_;
};
}
