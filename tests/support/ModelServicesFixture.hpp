#pragma once
#include <hksr/model/ModelServices.hpp>
#include <hksr/model/ModelServiceBindings.hpp>

inline std::shared_ptr<const hksr::api::ModelServices> TestModelServices(
    hksr::api::ReferenceMethods references,hksr::api::UnityLifetimeMethods lifetime={},
    hksr::api::RendererMethods renderers={},hksr::api::EntityAccessMethods entities={},
    hksr::api::ModelAccess models=hksr::api::ModelAccess({}),
    hksr::api::ArtAdmission admission=hksr::api::ArtAdmission({})) {
    return std::make_shared<hksr::api::ModelServices>(hksr::api::ModelServiceBindings{
        references,lifetime,renderers,entities,std::move(models),std::move(admission)});
}
