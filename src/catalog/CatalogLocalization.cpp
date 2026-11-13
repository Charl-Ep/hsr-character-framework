#include <hksr/framework/catalog/CatalogBrowser.hpp>
#include <set>

namespace hksr::framework {
void CatalogBrowser::PublishLocalizationError() {
    const auto error = language_error_.empty() ? names_error_ : language_error_;
    if (state_.localization_error != error) {
        state_.localization_error = error;
        publication_dirty_ = true;
    }
}
void CatalogBrowser::PollLanguage(std::chrono::steady_clock::time_point now) {
    // During a sliced refresh, all reads in a frame must use its captured
    // language. Idle polling does not need to run at render/frame frequency.
    const bool refreshing = name_refresh_ && !name_refresh_->waiting_for_language;
    if (!readers_.text_language || (!refreshing && now < next_language_poll_))
        return;
    next_language_poll_ = now + std::chrono::milliseconds(500);
    api::TextInput result;
    try {
        result = readers_.text_language();
    } catch (const std::exception& error) {
        result.error = error.what();
    }
    language_error_ = result.available && !result.value.empty()
                          ? std::string{}
                          : (result.error.empty() ? "official text language unavailable" : result.error);
    PublishLocalizationError();
    if (!language_error_.empty()) {
        if (name_refresh_)
            name_refresh_->waiting_for_language = true;
        return;
    }
    if (name_refresh_)
        name_refresh_->waiting_for_language = false;
    if (result.value == observed_text_language_)
        return;
    observed_text_language_ = result.value;
    if (state_.rows)
        BeginNameRefresh(observed_text_language_);
    else {
        state_.text_language = observed_text_language_;
        publication_dirty_ = true;
    }
}
void CatalogBrowser::BeginNameRefresh(const std::string& language) {
    NameRefresh work;
    work.language = language;
    work.rows = std::make_shared<api::CharacterCatalog>(*state_.rows);
    if (state_.skins)
        work.skins = std::make_shared<api::CharacterSkinCatalog>(*state_.skins);
    std::set<std::pair<api::CharacterNameKind, std::uint32_t>> seen;
    auto enqueue = [&](api::CharacterNameKind kind, auto id) {
        if (seen.emplace(kind, id).second)
            work.pending.emplace_back(kind, id);
    };
    for (const auto& row : work.rows->npcs.rows)
        enqueue(api::CharacterNameKind::Npc, row.id);
    for (const auto& row : work.rows->players.rows)
        enqueue(api::CharacterNameKind::Player, row.avatar);
    if (work.skins)
        for (const auto& row : work.skins->rows)
            enqueue(api::CharacterNameKind::Skin, row.skin_id);
    name_refresh_ = std::move(work);
}
bool CatalogBrowser::AdvanceNameRefresh() {
    if (!name_refresh_ || name_refresh_->waiting_for_language)
        return false;
    auto& work = *name_refresh_;
    if (!work.pending.empty()) {
        ++work_.name_steps;
        const auto [kind, id] = work.pending.front();
        work.pending.pop_front();
        api::CharacterDisplayName names;
        if (readers_.display_name)
            try {
                names = readers_.display_name(kind, id);
            } catch (const std::exception& error) {
                work.issues.push_back(std::to_string(id) + ": " + error.what());
                return true;
            }
        auto assign = [&](std::string& target, const api::TextInput& input, const char* field) {
            if (input.available)
                target = input.value; // An official empty name is a valid value.
            else
                work.issues.push_back(std::to_string(id) + " " + field + ": " +
                                      (input.error.empty() ? "display name unavailable" : input.error));
        };
        switch (kind) {
        case api::CharacterNameKind::Npc:
            for (auto& row : work.rows->npcs.rows)
                if (row.id == id)
                    assign(row.name, names.name, "name");
            break;
        case api::CharacterNameKind::Player:
            for (auto& row : work.rows->players.rows)
                if (row.avatar == id) {
                    assign(row.name, names.name, "name");
                    assign(row.path_name, names.path_name, "path");
                    assign(row.default_skin_name, names.default_skin_name, "default skin");
                }
            break;
        case api::CharacterNameKind::Skin:
            for (auto& row : work.skins->rows)
                if (row.skin_id == id)
                    assign(row.name, names.name, "name");
            break;
        }
        return true;
    }
    // Verify after the final item too: a reader may have observed a language
    // change while servicing that last call.
    api::TextInput language;
    try {
        language = readers_.text_language();
    } catch (const std::exception& error) {
        language.error = error.what();
    }
    if (!language.available || language.value.empty()) {
        language_error_ = language.error.empty() ? "official text language unavailable" : language.error;
        work.waiting_for_language = true;
        PublishLocalizationError();
        return false; // Retry at the next poll; source/action work can continue.
    }
    if (language.value != work.language) {
        observed_text_language_ = language.value;
        BeginNameRefresh(observed_text_language_);
        return true;
    }
    names_error_ = work.issues.empty() ? std::string{}
                                     : work.issues.front() + " (" + std::to_string(work.issues.size()) + " issues)";
    language_error_.clear();
    state_.text_language = work.language;
    state_.rows = std::move(work.rows);
    state_.skins = std::move(work.skins);
    name_refresh_.reset();
    PublishLocalizationError();
    publication_dirty_ = true;
    return true;
}
}
