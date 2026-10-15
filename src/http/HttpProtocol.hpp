#pragma once
#include <hksr/framework/application/ApplicationService.hpp>
#include <nlohmann/json.hpp>

namespace hksr::framework::http {
using Json=nlohmann::json;
std::uint64_t Id(const Json&); // decimal strings preserve uint64 in JS clients
CharacterCommand Command(const Json&);
Json Reply(const CommandReply&);
Json Snapshot(const ApplicationSnapshot&);
Json Catalog(const ApplicationSnapshot&);
Json Actions(const ApplicationSnapshot&,std::uint64_t character);
Json Updates(const ApplicationUpdates&);
std::string Cursor(const ApplicationCursor&);
ApplicationCursor ParseCursor(const std::string&);
std::string Event(const ApplicationUpdates&);
}
