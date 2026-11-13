#include "VisualLayoutCodec.hpp"
#include <nlohmann/json.hpp>
#include <stdexcept>
namespace hksr::framework::visual_layout_codec {
namespace {
std::string Name(const nlohmann::json& value) {
    const auto text = value.get<std::string>();
    if (text.empty() || text.find('\0') != std::string::npos)
        throw std::invalid_argument("invalid layout effect key");
    return text;
}
}
std::optional<VisualEffectLayout> Parse(const nlohmann::json& row) {
    if (row.at("op") != "effect.layout")
        return {};
    VisualEffectLayout value;
    value.effect = Name(row.at("effect"));
    for (auto field = row.begin(); field != row.end(); ++field)
        if (field.key() != "as" && field.key() != "wait" &&
            field.key() != "op" && field.key() != "source" && field.key() != "when" && field.key() != "context" &&
            field.key() != "effect" && field.key() != "target" && field.key() != "command" && field.key() != "fade" &&
            field.key() != "perform")
            throw std::invalid_argument("unknown layout field: " + field.key());
    const auto command = Name(row.at("command"));
    value.fade = row.value("fade", false);
    if (command == "hide")
        value.action = api::EffectLayoutAction::Hide;
    else if (command == "show")
        value.action = api::EffectLayoutAction::Show;
    else if (command == "perform") {
        value.action = api::EffectLayoutAction::Perform;
        if (!row.at("perform").is_number_integer())
            throw std::invalid_argument("layout perform must be an integer");
        const auto perform = row.at("perform").get<int>();
        if (perform < 0 || perform > 3)
            throw std::invalid_argument("unknown layout perform");
        value.perform = static_cast<api::EffectLayoutPerform>(perform);
    } else if (command == "transfer") {
        value.action = api::EffectLayoutAction::Transfer;
        value.target = Name(row.at("target"));
        if (value.target == value.effect)
            throw std::invalid_argument("layout transfer requires distinct effect keys");
    } else
        throw std::invalid_argument("unknown layout command");
    if (value.action != api::EffectLayoutAction::Transfer && row.contains("target"))
        throw std::invalid_argument("target requires layout transfer");
    if (value.action != api::EffectLayoutAction::Perform && row.contains("perform"))
        throw std::invalid_argument("perform index requires perform command");
    if ((value.action == api::EffectLayoutAction::Hide || value.action == api::EffectLayoutAction::Perform) &&
        row.contains("fade"))
        throw std::invalid_argument("fade requires layout show or transfer");
    return value;
}
}
