#include "../../src/presentation/TimelinePolicy.hpp"
#include <stdexcept>
void TestTimelinePolicy() {
    using hksr::framework::OnlyAuditedEnvironmentTracks;
    auto check=[](bool v){if(!v)throw std::runtime_error("Timeline policy classification failed");};
    hksr::api::TimelineAssetInfo r;r.complete=true;
    check(!OnlyAuditedEnvironmentTracks(r));
    r.tracks={{"UnityEngine.Timeline.MarkerTrack",false,0}};check(!OnlyAuditedEnvironmentTracks(r));
    r.tracks.push_back({"RPGTools.Timeline.LightControlTrack",false,0});check(OnlyAuditedEnvironmentTracks(r));
    r.tracks.push_back({"RPGTools.Timeline.EnvironmentSyetem.EnviroSysAdjustLevelTrack",false,0});check(OnlyAuditedEnvironmentTracks(r));
    r.tracks.push_back({"Test.LightDecoration",true,0});check(OnlyAuditedEnvironmentTracks(r));
    r.tracks.back().muted=false;check(!OnlyAuditedEnvironmentTracks(r));r.tracks.pop_back();
    r.tracks[0].markers=1;check(!OnlyAuditedEnvironmentTracks(r));r.tracks[0].markers=0;
    r.tracks.push_back({"UnityEngine.Timeline.AnimationTrack",false,0});check(!OnlyAuditedEnvironmentTracks(r));r.tracks.pop_back();
    r.complete=false;check(!OnlyAuditedEnvironmentTracks(r));
}
