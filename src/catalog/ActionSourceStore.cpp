#include <hksr/framework/catalog/ActionSourceStore.hpp>
#include <Windows.h>
#include <fstream>
#include <stdexcept>
#include <chrono>
#include <algorithm>

namespace hksr::framework {
namespace {
std::string Read(const std::filesystem::path& file) {
    std::ifstream in(file, std::ios::binary);
    if (!in)
        throw std::runtime_error("cannot read " + file.u8string());
    std::string text((std::istreambuf_iterator<char>(in)), {});
    if (in.bad())
        throw std::runtime_error("read failed: " + file.u8string());
    return text;
}
}
bool ReusableActionSource(const api::ActionConfigInput& value) {
    return value.loaded && value.issues.empty();
}
bool ReusableActionSource(const api::AbilitySources& value) {
    return std::none_of(value.issues.begin(), value.issues.end(),
                        [](const auto& issue) { return issue.kind == api::AbilitySourceIssueKind::ReadFailure; });
}
ActionSourceStore::ActionSourceStore(std::filesystem::path packaged, std::filesystem::path local,
                                     std::function<void(const std::string&)> report)
    : packaged_(std::move(packaged)), local_(std::move(local)), report_(std::move(report)) {}
void ActionSourceStore::Load() {
    const auto start = std::chrono::steady_clock::now();
    for (const auto& path : {packaged_, local_}) {
        try {
            if (!path.empty() && std::filesystem::exists(path))
                loaded_.push_back(DecodeActionSources(Read(path)));
        } catch (const std::exception& e) {
            if (report_)
                report_("action sources ignored: " + std::string(e.what()));
        }
    }
    if (report_)
        report_("action sources files=" + std::to_string(loaded_.size()) + " load_us=" +
                std::to_string(
                    std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - start)
                        .count()));
}
void ActionSourceStore::Bind(ActionSourceIdentity identity) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (bound_)
        throw std::logic_error("action source identity already bound");
    if (identity.revision.empty())
        throw std::invalid_argument("action source identity incomplete");
    data_.identity = std::move(identity);
    bound_ = true;
    for (auto& source : loaded_)
        if (source.identity == data_.identity) {
            for (const auto& key : source.invalid_documents) {
                data_.documents.erase(key);
                data_.invalid_documents.insert(key);
            }
            for (const auto& key : source.invalid_abilities) {
                data_.abilities.erase(key);
                data_.invalid_abilities.insert(key);
            }
            for (auto& row : source.documents)
                data_.documents.insert_or_assign(row.first, std::move(row.second));
            for (auto& row : source.abilities)
                data_.abilities.insert_or_assign(row.first, std::move(row.second));
        }
    loaded_.clear();
    if (report_)
        report_("action sources bound documents=" + std::to_string(data_.documents.size()) +
                " abilities=" + std::to_string(data_.abilities.size()));
}
std::shared_ptr<const api::ActionConfigInput> ActionSourceStore::Document(const ActionSourceKey& key) {
    std::lock_guard<std::mutex> lock(mutex_);
    const auto found = data_.documents.find(key);
    if (found == data_.documents.end() || !ReusableActionSource(*found->second)) {
        ++stats_.document_misses;
        return {};
    }
    ++stats_.document_hits;
    return found->second;
}
std::shared_ptr<const api::AbilitySources> ActionSourceStore::Ability(const std::string& key) {
    std::lock_guard<std::mutex> lock(mutex_);
    const auto found = data_.abilities.find(key);
    if (found == data_.abilities.end() || !ReusableActionSource(*found->second)) {
        ++stats_.ability_misses;
        return {};
    }
    ++stats_.ability_hits;
    return found->second;
}
void ActionSourceStore::Put(const api::ActionConfigInput& value) {
    auto copy = std::make_shared<const api::ActionConfigInput>(value);
    std::lock_guard<std::mutex> lock(mutex_);
    if (!bound_)
        return;
    data_.documents.insert_or_assign(ActionSourceKey{value.kind, value.path}, std::move(copy));
    dirty_ = true;
}
void ActionSourceStore::Put(const std::string& name, const api::AbilitySources& value) {
    auto copy = std::make_shared<const api::AbilitySources>(value);
    std::lock_guard<std::mutex> lock(mutex_);
    if (!bound_)
        return;
    data_.abilities.insert_or_assign(name, std::move(copy));
    dirty_ = true;
}
void ActionSourceStore::Erase(const ActionSourceKey& key) {
    std::lock_guard<std::mutex> lock(mutex_);
    data_.documents.erase(key);
    data_.invalid_documents.insert(key);
    flush_ = dirty_ = true;
}
void ActionSourceStore::EraseAbility(const std::string& name) {
    std::lock_guard<std::mutex> lock(mutex_);
    data_.abilities.erase(name);
    data_.invalid_abilities.insert(name);
    flush_ = dirty_ = true;
}
void ActionSourceStore::Flush() {
    std::lock_guard<std::mutex> lock(mutex_);
    flush_ = dirty_;
}
ActionSourceStore::Stats ActionSourceStore::Statistics() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return stats_;
}
void ActionSourceStore::Pump() {
    ActionSourceData copy;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!bound_ || !flush_)
            return;
        copy = data_;
        flush_ = dirty_ = false;
    }
    try {
        const auto text = EncodeActionSources(copy);
        std::filesystem::create_directories(local_.parent_path());
        auto temporary = local_;
        temporary += L".tmp";
        {
            std::ofstream file(temporary, std::ios::binary | std::ios::trunc);
            file.exceptions(std::ios::badbit | std::ios::failbit);
            file.write(text.data(), static_cast<std::streamsize>(text.size()));
            file.close();
        }
        if (!MoveFileExW(temporary.c_str(), local_.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
            throw std::system_error(GetLastError(), std::system_category(), "save action sources");
        if (report_)
            report_("action sources saved documents=" + std::to_string(copy.documents.size()) +
                    " abilities=" + std::to_string(copy.abilities.size()) + " bytes=" + std::to_string(text.size()));
    } catch (const std::exception& e) {
        if (report_)
            report_("action sources save failed: " + std::string(e.what()));
    }
}
}
