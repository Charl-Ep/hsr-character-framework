#include "ScriptComposition.hpp"
#include <nlohmann/json.hpp>
#include <set>
#include <stdexcept>

namespace hksr::framework::script_codec {
namespace {
using Json = nlohmann::json;
void Object(const Json& value, const std::string& name) {
    if (!value.is_object())
        throw std::invalid_argument(name + " must be an object");
}
// Definition maps merge by name. Only an explicit local definition overrides
// imported content; conflicting imports do not silently depend on file order.
void Definitions(Json& output, const Json& additions, bool local, bool fields = false) {
    Object(additions, "imported definitions");
    if (output.is_null()) output = Json::object();
    for (auto it = additions.begin(); it != additions.end(); ++it) {
        if (output.contains(it.key()) && output.at(it.key()) != it.value()) {
            if (!local)
                throw std::invalid_argument("conflicting imported definition: " + it.key());
            if (fields) {
                Object(it.value(), it.key());
                output[it.key()].update(it.value());
                continue;
            }
        }
        output[it.key()] = it.value();
    }
}
void Merge(Json& output, const Json& source, bool local) {
    Object(source, "script definitions");
    for (auto it = source.begin(); it != source.end(); ++it) {
        const auto& key = it.key();
        if (key == "imports") continue;
        if (key == "resources" || key == "defaults") {
            Object(it.value(), key);
            for (auto part = it.value().begin(); part != it.value().end(); ++part)
                Definitions(output[key][part.key()], part.value(), local);
        } else if (key == "functions" || key == "templates" || key == "states") {
            Definitions(output[key], it.value(), local, key == "states");
        } else output[key] = it.value();
    }
}
struct Composition {
    const ScriptImportReader& read;
    std::set<std::string> active;
    Json Collect(const Json& document, bool module = false) {
        Object(document, "script");
        if (module)
            for (auto it = document.begin(); it != document.end(); ++it)
                if (it.key() != "imports" && it.key() != "resources" && it.key() != "defaults" &&
                    it.key() != "states" && it.key() != "functions" && it.key() != "templates")
                    throw std::invalid_argument("unknown module field: " + it.key());
        Json output = Json::object();
        const auto imports = document.value("imports", Json::array());
        if (!imports.is_array()) throw std::invalid_argument("imports must be an array");
        for (const auto& item : imports) {
            const auto path = item.get<std::string>();
            if (path.empty() || path.find('\0') != std::string::npos)
                throw std::invalid_argument("import path is empty or invalid");
            if (!read) throw std::invalid_argument("script imports require a reader: " + path);
            if (!active.insert(path).second) throw std::invalid_argument("cyclic script import: " + path);
            try {
                Merge(output, Collect(Json::parse(read(path)), true), false);
            } catch (const std::exception& error) {
                throw std::invalid_argument(path + ": " + error.what());
            }
            active.erase(path);
        }
        Merge(output, document, true);
        return output;
    }
};
Json Action(const Json& source, const Json& templates, std::set<std::string>& active) {
    Object(source, "action");
    if (!source.contains("use")) return source;
    const auto name = source.at("use").get<std::string>();
    if (!templates.contains(name)) throw std::invalid_argument("unknown action template: " + name);
    if (!active.insert(name).second) throw std::invalid_argument("cyclic action template: " + name);
    auto result = Action(templates.at(name), templates, active);
    active.erase(name);
    for (auto it = source.begin(); it != source.end(); ++it) {
        if (it.key() == "use") continue;
        if (it.key() == "inputs") Definitions(result[it.key()], it.value(), true, true);
        else if (it.key() == "presentation" || it.key() == "bindings") {
            Object(it.value(), it.key());
            if (result[it.key()].is_null()) result[it.key()] = Json::object();
            result[it.key()].update(it.value());
        } else result[it.key()] = it.value();
    }
    return result;
}
}
Json Compose(Json document, const ScriptImportReader& reader) {
    auto result = Composition{reader, {}}.Collect(document);
    const auto templates = result.value("templates", Json::object());
    Object(templates, "templates");
    if (result.contains("actions")) {
        Object(result.at("actions"), "actions");
        for (auto& action : result["actions"]) {
            std::set<std::string> active;
            action = Action(action, templates, active);
        }
    }
    result.erase("templates");
    return result;
}
}
