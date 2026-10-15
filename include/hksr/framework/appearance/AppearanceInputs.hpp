#pragma once
#include "../assets/CharacterPackage.hpp"
#include "../assets/RendererBindings.hpp"
#include <hksr/animation/AdventureConfig.hpp>
#include <hksr/model/ArtAdmission.hpp>
#include <map>
namespace hksr::framework {
struct ConfigurationInput {
    api::ObjectResult load;
    api::AdventureConfigSnapshot data;
    bool ready=false;
    std::string error;
};
struct WeaponDefaultsResult {
    api::CallResult apply;
    bool attempted=false,no_values=false;
    std::string error;
};
// Source configuration only. Does not register events, select Animator states,
// or change shared config. Same source path is loaded once per preparation.
class AppearanceInputs {
public:
    explicit AppearanceInputs(const api::ModelServices& services):roots_(services.CreateRoots()){}
    void Prepare(api::ManagedObjects& objects,const api::AdventureConfig& config,const Appearance& source);
    // Invoke only after official adoption installed this model's attach mapping.
    WeaponDefaultsResult ApplyWeaponDefaults(const api::ArtAdmission& admission,api::CharacterModelComponent* model) const;
    const ConfigurationInput& Character() const { return character_; }
    const ConfigurationInput& Weapons() const { return weapons_; }
    BindingResult Close();
private:
    void* Keep(api::ObjectResult value);
    std::unique_ptr<api::ManagedRoots> roots_;
    std::vector<api::ManagedRootId> roots_ids_;
    ConfigurationInput character_,weapons_;
    bool started_=false,closing_=false;
};
}
