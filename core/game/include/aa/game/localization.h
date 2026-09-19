// UI::Localization (docs/06 §3): the active locale, the text bundle lookup and the localised image names.
#pragma once

#include "aa/data/ui_loaders.h"

#include <string>
#include <vector>

namespace aa::game {

// The five bundle locales in the order the text bundles list them.
const std::vector<std::string>& bundleLocales();

// UI::Localization::Init [verified]: the first of the OS's preferred languages that names a bundle locale
// exactly wins, otherwise en_EN. The remake also accepts a bare language code / a POSIX locale string
// ("de", "de_AT", "fr_FR.UTF-8") by its language part.
std::string chooseLocale(const std::vector<std::string>& preferred);
// The environment's preferred languages (LC_ALL / LC_MESSAGES / LANG); the platform layer appends the OS's
// own list after them (CFLocaleCopyPreferredLanguages on macOS, the user default locale on Windows).
std::vector<std::string> systemPreferredLanguages();

class Localization {
public:
    void setLocale(std::string locale, aa::data::TextTable table) {
        locale_ = std::move(locale);
        table_ = std::move(table);
    }
    const std::string& locale() const { return locale_; }
    // The two-letter suffix of localised sprite names: "EN", "FR", "IT", "DE", "ES".
    std::string languageSuffix() const;
    // UI::Localization::GetLocalizedString: the text, or the id itself when unknown.
    const std::string& text(const std::string& id) const;
    bool has(const std::string& id) const { return table_.count(id) != 0; }
    // UI::Localization::GetLocalizedImageString: `<name>_<LANG>` (BEST_RESULT → BEST_RESULT_EN).
    std::string imageName(const std::string& name) const { return name + "_" + languageSuffix(); }

private:
    std::string locale_ = "en_EN";
    aa::data::TextTable table_;
};

}  // namespace aa::game
