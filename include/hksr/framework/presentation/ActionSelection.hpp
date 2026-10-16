#pragma once
#include "../character/SceneCharacterState.hpp"
#include "../catalog/ActionCatalogTypes.hpp"
#include <stdexcept>
#include <type_traits>
#include <variant>

namespace hksr::framework {
struct CharacterActionCatalog {
    std::uint64_t revision=0;
    ActionCatalogStatus actions;
};
struct ConfiguredActionKey {ActionDomain domain;std::string state;};
struct ReadyActionKey {std::string source;std::size_t pose=0;};
struct CatalogActionSelection {
    std::uint64_t revision=0;
    std::variant<ConfiguredActionKey,ReadyActionKey> key;
};
// Shared application rule, evaluated on the official frame against this
// logical instance's current catalog. Transports never assemble retirements or
// carry cached ReadyActions back into execution.
inline PlacedAction ResolveCatalogAction(const CharacterActionCatalog& catalog,const CatalogActionSelection& selection) {
    if(!selection.revision || selection.revision!=catalog.revision)
        throw std::runtime_error("action source changed; read this instance's catalog again");
    return std::visit([&](const auto& key)->PlacedAction {
        using T=std::decay_t<decltype(key)>;
        const auto domain=[&] {
            if constexpr(std::is_same_v<T,ConfiguredActionKey>)return key.domain;
            else return ActionDomain::Alternate;
        }();
        if(!catalog.actions.Ready(domain))throw std::runtime_error("action data is still being read");
        if constexpr(std::is_same_v<T,ConfiguredActionKey>) {
            for(const auto& row:catalog.actions.proposals)
                if(row.domain==key.domain && row.state==key.state)return {row.domain,row.Request()};
            throw std::runtime_error("configured action absent from this instance's catalog");
        }else {
            for(const auto& set:catalog.actions.ready_actions)if(set->source==key.source) {
                if(key.pose>=set->poses.size())throw std::runtime_error("ready pose absent from this instance's source");
                return {ActionDomain::Alternate,ReadyState{set,key.pose}};
            }
            throw std::runtime_error("ready source absent from this instance's catalog");
        }
    },selection.key);
}
}
