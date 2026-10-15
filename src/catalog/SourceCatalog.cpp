#include <hksr/framework/catalog/SourceCatalog.hpp>
#include <hksr/framework/catalog/OfficialSourcePreferences.hpp>
#include <algorithm>
#include <set>
#include <stdexcept>
namespace hksr::framework {
void SourceCatalog::SetIdentity(std::string path,api::TextInput identity) {
    identities_.insert_or_assign(std::move(path),std::move(identity));
}
void SourceCatalog::ForgetUnavailableIdentities() {
    for(auto it=identities_.begin();it!=identities_.end();)
        if(!it->second.available) it=identities_.erase(it);else ++it;
}
FoundationChoice SourceCatalog::Foundation(const api::PlayerCatalogRow& player,std::optional<std::uint32_t> explicit_npc) const {
    FoundationChoice out;
    if(explicit_npc) {
        // Same contract as an authored official-player package: the caller
        // chooses an actual NPC row. FreeStyle identity is only an AUTOMATIC
        // discovery aid, not an input used by MapNPC or appearance construction.
        // This does not certify compatibility or invent an automatic mapping.
        out.explicit_choice=true;
        for(const auto& npc:rows_.npcs.rows)if(npc.id==*explicit_npc) {
            out.candidates.push_back(npc.id);out.selected=npc;
        }
        if(out.candidates.size()>1) {out.selected.reset();out.status=SourceResolution::Ambiguous;}
        else if(out.selected) out.status=SourceResolution::Ready;
        return out;
    }
    for(const auto& preference:OfficialSourcePreferences()) {
        if(player.character_config!=preference.player_config) continue;
        for(const auto& npc:rows_.npcs.rows)
            if(npc.character_config==preference.npc_config && npc.model_config==preference.npc_model) {
                out.candidates.push_back(npc.id);out.selected=npc;
            }
        if(!out.candidates.empty()) {
            out.preference=preference.label;
            out.notes.push_back("Framework默认来源选择："+preference.label+"（非官方配对表）");
            if(out.candidates.size()==1) out.status=SourceResolution::Ready;
            else {out.selected.reset();out.status=SourceResolution::Ambiguous;}
            return out;
        }
        out.notes.push_back("默认NPC来源不在当前目录，继续自动查询："+preference.label);
    }
    auto read=[&](const std::string& path)->const api::TextInput* {
        auto found=identities_.find(path);
        if(found==identities_.end()) {
            if(std::find(out.needed_configs.begin(),out.needed_configs.end(),path)==out.needed_configs.end()) out.needed_configs.push_back(path);
            return nullptr;
        }
        if(!found->second.available || found->second.value.empty()) {
            out.errors.push_back(path+": "+(found->second.error.empty()?"FreeStyle identity absent":found->second.error)); return nullptr;
        }
        return &found->second;
    };
    const auto source=read(player.character_config);
    if(!source) { out.status=out.needed_configs.empty()?SourceResolution::Incomplete:SourceResolution::Pending; return out; }
    // Existing framework policy: try the same-number NPC as a candidate, but
    // accept it ONLY after checking the declared FreeStyle identity. This is
    // not an official NPC-ID/Avatar-ID equivalence rule. Duplicate IDs do not
    // select the first row. It avoids scanning every config on the common path.
    if(!explicit_npc) {
        const api::NpcCatalogRow* preferred=nullptr; unsigned count=0;
        for(const auto& npc:rows_.npcs.rows) if(npc.id==player.avatar) { preferred=&npc; ++count; }
        if(count==1) {
            const auto identity=read(preferred->character_config);
            if(!identity && !out.needed_configs.empty()) { out.status=SourceResolution::Pending; return out; }
            if(identity && identity->value==source->value) {
                out.status=SourceResolution::Ready; out.candidates={preferred->id}; out.selected=*preferred; return out;
            }
            out.errors.clear(); // the failed candidate is re-reported in a full search
        }
    }
    std::vector<api::NpcCatalogRow> matches;
    for(const auto& npc:rows_.npcs.rows) {
        const auto identity=read(npc.character_config);
        if(identity && identity->value==source->value) { matches.push_back(npc); out.candidates.push_back(npc.id); }
    }
    // Complete pending reads before choosing among available matching rows.
    // Missing data is diagnostic, not proof that an independently validated
    // foundation cannot work. Explicit choice need not read unrelated configs.
    if(!out.needed_configs.empty()) out.status=SourceResolution::Pending;
    else if(matches.size()>1) out.status=SourceResolution::Ambiguous;
    else if(matches.size()==1) { out.status=SourceResolution::Ready; out.selected=matches.front(); }
    else out.status=out.errors.empty()?SourceResolution::Missing:SourceResolution::Incomplete;
    if(!explicit_npc && (!rows_.npcs.enumerated || !rows_.npcs.issues.empty())) {
        if(out.status==SourceResolution::Missing) out.status=SourceResolution::Incomplete;
        out.errors.push_back("NPC table is partial; selection covers available verified identities only");
    }
    return out;
}
BattleSourceChoice SourceCatalog::Battle(std::uint32_t avatar) const {
    BattleSourceChoice out;
    for(const auto& row:rows_.avatars.rows) if(row.avatar==avatar) {
        const auto same=std::find_if(out.candidates.begin(),out.candidates.end(),[&](const auto& p) {
            return p.character_config==row.character_config && p.prefab==row.prefab;
        });
        if(same==out.candidates.end()) out.candidates.push_back(row);
    }
    if(out.candidates.size()>1) out.status=SourceResolution::Ambiguous;
    else if(!out.candidates.empty() && !out.candidates[0].character_config.empty() && !out.candidates[0].prefab.empty()) out.status=SourceResolution::Ready;
    else if(!rows_.avatars.enumerated || !rows_.avatars.issues.empty()) out.status=SourceResolution::Incomplete;
    return out;
}
std::vector<api::PlayerCatalogRow> SourceCatalog::PackageSources(const CharacterPackage& package) const {
    std::vector<api::PlayerCatalogRow> out;
    if(package.appearance) for(const auto& row:rows_.players.rows)
        if(row.character_config==package.appearance->character_config && row.prefab==package.appearance->template_prefab) out.push_back(row);
    return out;
}
CharacterPackage SourceCatalog::NpcPackage(const api::NpcCatalogRow& npc) {
    CharacterPackage out; out.id="official-npc:"+std::to_string(npc.id);
    out.display_name=out.id+" | "+npc.character_config; out.npc_id=npc.id; return out;
}
CharacterPackage SourceCatalog::PlayerPackage(const api::PlayerCatalogRow& player,const api::NpcCatalogRow& npc) {
    CharacterPackage out; out.id="official-player:"+std::to_string(player.id)+":"+std::to_string(player.enhanced)+":"+std::to_string(player.skin);
    out.display_name=out.id+" | "+player.character_config; out.npc_id=npc.id;
    out.avatar_id=player.avatar;
    out.appearance=Appearance{player.prefab,{},player.character_config,player.character_config,ShapeData::None}; return out;
}
CharacterPackage SourceCatalog::PlayerPackage(const api::PlayerCatalogRow& player) {
    auto result=PlayerPackage(player,api::NpcCatalogRow{});
    result.construction=ConstructionKind::StoryActor;
    return result;
}
BattlePresentation SourceCatalog::Presentation(const api::AvatarCatalogRow& avatar,const api::PresentationConfigInput& config) {
    if(config.path!=avatar.character_config || avatar.prefab.empty()) throw std::invalid_argument("presentation input does not match the selected official source");
    if(!config.event_files.available || !config.disabled_event_layers.available)
        throw std::invalid_argument("official presentation event inputs were not read");
    return {{avatar.prefab,config.event_files.values,config.disabled_event_layers.values,config.path},config.initial_state};
}
SourcePreparation::SourcePreparation(api::CharacterCatalog rows,api::OfficialCatalog& api)
    :SourcePreparation(std::move(rows),CatalogQueries{
        [&api](const auto& path) { return api.ReadAdventureIdentity(path); },
        [&api](const auto& path) { return api.ReadPresentationConfig(path); }}){}
SourcePreparation::SourcePreparation(api::CharacterCatalog rows,CatalogQueries queries)
    :catalog_(std::move(rows)),queries_(std::move(queries)){}
void SourcePreparation::Player(PlayerKey key,std::optional<std::uint32_t> npc) {
    result_={}; player_.reset(); package_.reset(); explicit_npc_=npc;
    unsigned matches=0;
    for(const auto& row:catalog_.Rows().players.rows) if(key.Matches(row)) { player_=row; ++matches; }
    if(matches!=1) {
        player_.reset(); result_.finished=true;
        result_.errors.push_back(matches?"selected Player tuple is ambiguous":"selected Player tuple is absent");
    }
}
void SourcePreparation::Package(CharacterPackage package) {
    result_={}; player_.reset(); explicit_npc_.reset(); package_=std::move(package);
    // Authored packages already declare their NPC foundation. No unrelated
    // automatic identity-discovery gate may invalidate that explicit choice.
    result_.character=package_;
}
void SourcePreparation::SelectBattle(std::uint32_t avatar) {
    result_.battle=catalog_.Battle(avatar);
    if(result_.battle.status!=SourceResolution::Ready) { result_.finished=true; return; }
    const auto& source=result_.battle.candidates.front();
    auto found=presentations_.find(source.character_config);
    if(found==presentations_.end()) {
        api::PresentationConfigInput input; input.path=source.character_config;
        try { input=queries_.presentation(source.character_config); }
        catch(const std::exception& error) { input.event_files.error=input.disabled_event_layers.error=error.what(); }
        found=presentations_.emplace(source.character_config,std::move(input)).first;
    }
    try { result_.presentation=SourceCatalog::Presentation(source,found->second); }
    catch(const std::exception& error) { result_.errors.push_back(error.what()); }
    result_.finished=true; // exploration source remains usable on battle errors
}
void SourcePreparation::Advance() {
    if(result_.finished) return;
    if(package_) {
        auto players=catalog_.PackageSources(*package_);
        if(players.empty()) { result_.finished=true; return; } // official NPC or custom package without a matching Player source
        const auto avatar=players.front().avatar;
        if(std::any_of(players.begin(),players.end(),[&](const auto& p) { return p.avatar!=avatar; })) {
            result_.errors.push_back("package resource pair has multiple Avatar identities"); result_.finished=true; return;
        }
        SelectBattle(avatar); return;
    }
    if(!player_) { result_.finished=true; return; }
    result_.character=SourceCatalog::PlayerPackage(*player_);
    SelectBattle(player_->avatar);
}
void SourcePreparation::Retry() {
    std::vector<std::string> paths;
    for(const auto& row:result_.battle.candidates)paths.push_back(row.character_config);
    InvalidateFailures(paths);
    if(player_) {const auto p=*player_;Player({p.id,p.enhanced,p.skin},explicit_npc_);}
    else if(package_) Package(*package_);
}
void SourcePreparation::InvalidateFailures(const std::vector<std::string>& presentations) {
    catalog_.ForgetUnavailableIdentities();
    for(const auto& path:presentations)presentations_.erase(path);
}
}
