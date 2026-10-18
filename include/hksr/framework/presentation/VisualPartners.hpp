#pragma once
#include "VisualProgram.hpp"
#include <hksr/entity/EntityCreationSession.hpp>

namespace hksr::framework {
struct VisualPartnerServices {
    std::function<std::unique_ptr<api::EntityCreationSession>()> create;
    std::function<std::unique_ptr<api::PartnerControlSession>(api::EntityToken)> controls;
    std::function<api::EntityOrigin(VisualEffectContext&,const std::string&)> target;
    std::function<void(const std::string&)> trace;
};
// One parent's partners, with explicit ownership by action/named context.
// The original SDK factory owns the model/events and native attachment tick.
class VisualPartners {
public:
    VisualPartners(api::EntityOrigin,std::map<std::string,VisualPartnerInput>,VisualPartnerServices);
    ~VisualPartners();
    VisualStepResult Execute(std::shared_ptr<VisualEffectContext>,const VisualPartnerOperation&);
    void End(const std::shared_ptr<VisualEffectContext>&);
    void BeginFrame(const api::WorldFrame& frame){frame_=frame;have_frame_=true;}
    void Service(const api::WorldFrame&);
    api::EntityOrigin Resolve(const std::string&) const;
    bool Drained() const;
private:
    struct Entry;
    bool Current(const std::shared_ptr<Entry>&) const;
    void Collect(const std::shared_ptr<Entry>&);
    void Report(Entry&);
    api::EntityOrigin owner_;
    std::map<std::string,VisualPartnerInput> inputs_;
    VisualPartnerServices services_;
    std::map<std::string,std::shared_ptr<Entry>> entries_;
    api::WorldFrame frame_;
    bool have_frame_=false,servicing_=false;
};
}
