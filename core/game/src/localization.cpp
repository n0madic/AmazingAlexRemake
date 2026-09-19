#include "aa/game/localization.h"

#include <cctype>
#include <cstdlib>

namespace aa::game {

const std::vector<std::string>& bundleLocales() {
    static const std::vector<std::string> locales = {"en_EN", "fr_FR", "it_IT", "de_DE", "es_ES"};
    return locales;
}

std::string chooseLocale(const std::vector<std::string>& preferred) {
    const std::vector<std::string>& locales = bundleLocales();
    for (const std::string& want : preferred) {
        for (const std::string& l : locales) {
            if (l == want) return l;
        }
    }
    // Remake extension: match the language part of "de", "de_AT", "de_DE.UTF-8".
    for (const std::string& want : preferred) {
        std::string lang;
        for (char c : want) {
            if (c == '_' || c == '-' || c == '.' || c == '@') break;
            lang.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
        }
        if (lang.empty()) continue;
        for (const std::string& l : locales) {
            if (l.substr(0, 2) == lang) return l;
        }
    }
    return "en_EN";
}

std::vector<std::string> systemPreferredLanguages() {
    std::vector<std::string> out;
    for (const char* var : {"LC_ALL", "LC_MESSAGES", "LANG"}) {
        const char* v = std::getenv(var);
        if (v && *v) out.emplace_back(v);
    }
    return out;
}

std::string Localization::languageSuffix() const {
    std::string s = locale_.substr(0, 2);
    for (char& c : s) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    return s.empty() ? std::string("EN") : s;
}

const std::string& Localization::text(const std::string& id) const {
    const auto it = table_.find(id);
    return it == table_.end() ? id : it->second;
}

}  // namespace aa::game
