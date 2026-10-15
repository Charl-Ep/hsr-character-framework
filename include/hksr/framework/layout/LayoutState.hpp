#pragma once
#include <cstdint>
#include <string>
#include <map>
#include <vector>

namespace hksr::framework {
enum class LayoutOperation {Refresh,Save,Load,Delete,RetryCurrent,KeepCurrent};
struct LayoutCommand {LayoutOperation operation;std::string name;bool overwrite=false;std::string scene;};
struct MapLayoutStatus {
    bool initialized=false,busy=false;
    std::string phase,error,storage_error,operation,name;
    std::uint64_t command=0,revision=0,saved_revision=0;
    std::vector<std::string> names;
};
struct LayoutSnapshot {bool available=false;std::uint64_t sequence=0;std::map<std::string,MapLayoutStatus> maps;};

}
