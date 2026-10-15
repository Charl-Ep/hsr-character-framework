#pragma once
#include <hksr/presentation/timeline/TimelineAsset.hpp>
namespace hksr::framework {
// Exact audited track semantics, not character names, filenames, or a broad
// "Light/Camera" substring blacklist. Mixed/unknown resources stay unchanged.
inline bool OnlyAuditedEnvironmentTracks(const api::TimelineAssetInfo& asset) {
    if(!asset.complete)return false;
    bool environment=false;
    for(const auto& track:asset.tracks) {
        if(track.muted)continue;
        if(track.markers!=0)return false;
        if(track.type=="UnityEngine.Timeline.MarkerTrack")continue;
        if(track.type!="RPGTools.Timeline.LightControlTrack" &&
            track.type!="RPGTools.Timeline.SceneLightControlTrack" &&
            track.type!="RPGTools.Timeline.EnvironmentSyetem.EnviroSysAdjustLevelTrack")return false;
        environment=true;
    }
    return environment;
}
}
