#pragma once
#include <hksr/control/PartnerControlSession.hpp>
#include <variant>

namespace hksr::framework {
struct VisualPartnerInput {std::string model,config,member;};
struct VisualPartnerCreate {};
struct VisualPartnerRemove {};
struct VisualPartnerAttach {
    std::string target,self_point,target_point;
    bool disable_when_invisible=false,use_target_scale=false;
    api::VisibilityFollow visibility=api::VisibilityFollow::None;
};
struct VisualPartnerAnimationFollow {std::string target;}; // empty clears
struct VisualPartnerVisibilityFollow {std::string target;api::VisibilityFollow flags=api::VisibilityFollow::None;};
using VisualPartnerCommand=std::variant<VisualPartnerCreate,VisualPartnerRemove,VisualPartnerAttach,
    VisualPartnerAnimationFollow,VisualPartnerVisibilityFollow,api::PartnerVisibility,api::PartnerIdleAnimation>;
struct VisualPartnerOperation {std::string partner;VisualPartnerCommand command;};
}
