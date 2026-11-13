#pragma once
#include <hksr/framework/application/ApplicationService.hpp>
#include <nlohmann/json.hpp>

namespace hksr::research {
// Private wire decoder. ApplicationService never receives a JSON value.
framework::CharacterCommand ParseDevelopmentCommand(const nlohmann::json& input);
}
