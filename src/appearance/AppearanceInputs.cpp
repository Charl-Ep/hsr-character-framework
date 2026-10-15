#include <hksr/framework/appearance/AppearanceInputs.hpp>
#include <stdexcept>
namespace hksr::framework {
void* AppearanceInputs::Keep(api::ObjectResult value) {
    if(!value.call.returned || !value.object) throw std::runtime_error("configuration input unavailable");
    roots_ids_.push_back(0); const auto id=roots_->Keep(value.object); roots_ids_.back()=id;
    if(!roots_->Status(id).ready) throw std::runtime_error("configuration root unavailable");
    return value.object;
}
void AppearanceInputs::Prepare(api::ManagedObjects& objects,const api::AdventureConfig& config,const Appearance& source) {
    if(started_ || closing_) throw std::logic_error("appearance input epoch is single-use");
    started_=true;
    std::map<std::string,ConfigurationInput> loaded;
    auto acquire=[&](const std::string& path) {
        const auto existing=loaded.find(path); if(existing!=loaded.end()) return existing->second;
        ConfigurationInput input;
        if(path.empty()) input.error="source configuration path absent";
        else try {
            auto* name=Keep(objects.NewStringUtf8(path.c_str()));
            input.load=config.Load(name); Keep(input.load);
            input.data=config.Read(input.load.object);
            // Keep actual array references without ASCII/length restrictions or
            // rebuilding an identical managed array. Fields are immutable inputs.
            input.ready=true;
            auto retain_field=[&](api::ObjectResult& field) {
                if(!field.call.returned || !field.object) return;
                try { Keep(field); }
                catch(const std::exception& error) { field.call.returned=false; input.error+=std::string(error.what())+"; "; }
            };
            retain_field(input.data.hidden_weapons); retain_field(input.data.event_paths);
        } catch(const std::exception& error) { input.error=error.what(); }
        loaded.emplace(path,input); return input;
    };
    character_=acquire(source.character_config);
    weapons_=acquire(source.weapon_config); // independent even if the first failed
}
WeaponDefaultsResult AppearanceInputs::ApplyWeaponDefaults(const api::ArtAdmission& admission,api::CharacterModelComponent* model) const {
    WeaponDefaultsResult out;
    if(closing_ || !weapons_.ready || !weapons_.data.hidden_weapons.call.returned) {
        out.error="weapon default input unavailable"; return out;
    }
    if(!weapons_.data.hidden_weapons.object) { out.no_values=true; return out; }
    if(!model) { out.error="weapon default target missing"; return out; }
    out.attempted=true; out.apply=admission.HideWeapons(model,weapons_.data.hidden_weapons.object);
    if(!out.apply.returned) out.error="official weapon default application failed";
    return out;
}
BindingResult AppearanceInputs::Close() {
    closing_=true; BindingResult out;
    for(auto id:roots_ids_) if(id && !roots_->Release(id).complete) out.errors.push_back("configuration root release unresolved");
    out.complete=out.errors.empty(); return out;
}
}
