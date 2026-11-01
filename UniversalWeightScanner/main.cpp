#include "RE/Skyrim.h"
#include "SKSE/SKSE.h"

#include <array>
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>

namespace UWS
{
    struct Category
    {
        RE::FormType type;
        const char* name;
        bool enabled = false;
        float weight = 0.0f;
    };

    std::array categories{
        Category{ RE::FormType::Armor, "Armor" },
        Category{ RE::FormType::Armor, "Jewelry" },
        Category{ RE::FormType::Weapon, "Weapon" },
        Category{ RE::FormType::Ammo, "Ammo" },
        Category{ RE::FormType::Misc, "Misc" },
        Category{ RE::FormType::AlchemyItem, "AlchemyItem" },
        Category{ RE::FormType::Ingredient, "Ingredient" },
        Category{ RE::FormType::Book, "Book" },
        Category{ RE::FormType::Scroll, "Scroll" },
        Category{ RE::FormType::SoulGem, "SoulGem" },
        Category{ RE::FormType::KeyMaster, "KeyMaster" },
        Category{ RE::FormType::Light, "Light" },
        Category{ RE::FormType::Apparatus, "Apparatus" },
        Category{ RE::FormType::Note, "Note" }
    };

    bool debugLog = false;

    std::string Trim(std::string s)
    {
        const auto first = s.find_first_not_of(" \t\r\n");
        if (first == std::string::npos) return {};
        const auto last = s.find_last_not_of(" \t\r\n");
        return s.substr(first, last - first + 1);
    }

    std::string Lower(std::string s)
    {
        std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) {
            return static_cast<char>(std::tolower(c));
            });
        return s;
    }

    std::filesystem::path ConfigPath()
    {
        return std::filesystem::path("Data") / "SKSE" / "Plugins" /
            "UniversalWeightScanner" / "UniversalWeightScanner.cfg";
    }

    void WriteDefaultConfig(const std::filesystem::path& path)
    {
        std::error_code ec;
        std::filesystem::create_directories(path.parent_path(), ec);
        std::ofstream out(path);
        if (!out) return;
        out << "; UniversalWeightScanner v1.0.1\n"
            "; Changes base-record weights in memory after data loading.\n"
            "; All categories are disabled by default.\n"
            "; Set Enabled=true for categories you want to change.\n"
            "; Enabled=false leaves a category unchanged.\n"
            "; Weight must be a finite number >= 0.\n\n"
            "[General]\nDebugLog=false\n";
        static constexpr std::pair<std::string_view, std::string_view> descriptions[]{
            { "Armor", "Armor and clothing excluding jewelry (rings, amulets and circlets)." },
            { "Jewelry", "Equippable rings (36), amulets (35), and circlets (42), including mod-added items using those slots." },
            { "Weapon", "Swords, axes, bows, daggers, staves and other weapons." },
            { "Ammo", "Arrows, bolts and other ammunition." },
            { "Misc", "Miscellaneous items such as clutter, gems, ingots and crafting materials." },
            { "AlchemyItem", "Potions, poisons, food and drinks." },
            { "Ingredient", "Alchemy ingredients such as flowers, mushrooms and animal parts." },
            { "Book", "Books, spell tomes and readable book-type items." },
            { "Scroll", "Magic scrolls." },
            { "SoulGem", "Empty and filled soul gems." },
            { "KeyMaster", "Keys." },
            { "Light", "Torches and other light-source records." },
            { "Apparatus", "Alchemy apparatus records (rarely used in Skyrim)." },
            { "Note", "Note-type records (may be unused; many readable notes are Books)." },
        };
        for (const auto& category : categories) {
            for (const auto& [name, description] : descriptions) {
                if (name == category.name) {
                    out << "\n; " << description << '\n';
                    break;
                }
            }
            out << "[" << category.name << "]\nEnabled="
                << (std::string_view(category.name) == "Jewelry" ? "true" : "false")
                << "\nWeight=0.0\n";
        }
    }

    void LoadConfig(std::ostream& log)
    {
        const auto path = ConfigPath();
        if (!std::filesystem::exists(path)) {
            WriteDefaultConfig(path);
            log << "INFO: Config missing; attempted default creation: " << path.string() << '\n';
        }

        log << "Config path: " << path.string() << '\n';
        std::ifstream in(path);
        if (!in) {
            log << "WARNING: Cannot read config. All changes disabled for safety.\n";
            for (auto& category : categories) category.enabled = false;
            return;
        }

        // Detect missing sections without replacing existing user settings.
        // New sections are appended after the existing config has been read.
        std::array<bool, categories.size()> foundSections{};
        std::string section, line;
        std::size_t lineNo = 0;
        while (std::getline(in, line)) {
            ++lineNo;
            line = Trim(line);
            if (line.empty() || line[0] == ';' || line[0] == '#') continue;
            if (line.front() == '[' && line.back() == ']') {
                section = Lower(Trim(line.substr(1, line.size() - 2)));
                for (std::size_t i = 0; i < categories.size(); ++i) {
                    if (section == Lower(categories[i].name)) foundSections[i] = true;
                }
                continue;
            }
            const auto pos = line.find('=');
            if (pos == std::string::npos) continue;
            const auto key = Lower(Trim(line.substr(0, pos)));
            auto value = Trim(line.substr(pos + 1));
            const auto comment = value.find_first_of(";#");
            if (comment != std::string::npos) value = Trim(value.substr(0, comment));
            const auto lowered = Lower(value);
            auto parseBool = [&]() -> std::optional<bool> {
                if (lowered == "true" || lowered == "1") return true;
                if (lowered == "false" || lowered == "0") return false;
                return std::nullopt;
                };
            if (section == "general") {
                if (key == "debuglog") {
                    if (auto b = parseBool()) debugLog = *b;
                }
                continue;
            }
            for (auto& category : categories) {
                if (section != Lower(category.name)) continue;
                if (key == "enabled") {
                    if (auto b = parseBool()) category.enabled = *b;
                    else {
                        category.enabled = false;
                        log << "WARNING: Invalid Enabled on line " << lineNo
                            << " [" << category.name << "]; category disabled for safety\n";
                    }
                }
                else if (key == "weight") {
                    try {
                        std::size_t parsed = 0;
                        const float weight = std::stof(value, &parsed);
                        if (parsed == value.size() && std::isfinite(weight) && weight >= 0.0f)
                            category.weight = weight;
                        else
                            log << "Invalid Weight on line " << lineNo << '\n';
                    }
                    catch (...) {
                        log << "Invalid Weight on line " << lineNo << '\n';
                    }
                }
                break;
            }
        }

        in.close();
        bool needsUpdate = false;
        for (bool found : foundSections) {
            if (!found) { needsUpdate = true; break; }
        }
        if (needsUpdate) {
            std::ofstream append(path, std::ios::app);
            if (!append) {
                log << "WARNING: Could not append missing config categories\n";
            }
            else {
                for (std::size_t i = 0; i < categories.size(); ++i) {
                    if (foundSections[i]) continue;
                    const bool enableNewJewelry = std::string_view(categories[i].name) == "Jewelry";
                    append << "\n; Added automatically by UniversalWeightScanner v1.0.2.\n"
                        << "[" << categories[i].name << "]\n"
                        << "Enabled=" << (enableNewJewelry ? "true" : "false") << "\nWeight=0.0\n";
                    if (enableNewJewelry) categories[i].enabled = true;
                    log << "INFO: Added missing config section [" << categories[i].name
                        << "] (Enabled=" << (enableNewJewelry ? "true" : "false") << ")\n";
                }
            }
        }
    }

    // Skyrim biped slot numbers are represented as bits starting at slot 30.
    // Only exclusive jewelry slots (35, 36, 42) qualify; multi-slot armor
    // combining jewelry and non-jewelry slots remains in Armor.
    bool IsJewelry(const RE::TESObjectARMO* armor)
    {
        if (!armor) return false;
        constexpr std::uint32_t jewelryMask =
            (1u << (35 - 30)) | (1u << (36 - 30)) | (1u << (42 - 30));
        const auto slots = armor->GetSlotMask().underlying();
        return (slots & jewelryMask) != 0 && (slots & ~jewelryMask) == 0;
    }

    void ScanAndApply()
    {
        const auto directory = SKSE::log::log_directory();
        if (!directory) return;
        const auto logPath = *directory / "UniversalWeightScanner.log";
        std::ofstream log(logPath, std::ios::trunc);
        if (!log) return;

        log << "UniversalWeightScanner v1.0.2\n"
            << "Mode: Configurable weights\n"
            << "Event: kDataLoaded\n"
            << "================================\n";
        LoadConfig(log);
        log << "DebugLog: " << (debugLog ? "true" : "false") << "\n\n";

        auto* dataHandler = RE::TESDataHandler::GetSingleton();
        if (!dataHandler) {
            log << "ERROR: TESDataHandler unavailable; no changes applied\n";
            return;
        }

        std::uint64_t totalRecords = 0, totalSupported = 0;
        std::uint64_t totalChanged = 0, totalUnchanged = 0, totalUnsupported = 0;
        std::uint64_t skippedCategories = 0, enabledCategories = 0;

        for (const auto& category : categories) {
            if (!category.enabled) {
                ++skippedCategories;
                log << "[" << category.name << "] SKIPPED (Enabled=false)\n";
                continue;
            }

            ++enabledCategories;
            std::uint64_t records = 0, supported = 0;
            std::uint64_t changed = 0, unchanged = 0, unsupported = 0;
            auto& forms = dataHandler->GetFormArray(category.type);

            log << "[" << category.name << "] Target weight: " << category.weight << '\n';
            for (auto* form : forms) {
                if (!form) continue;
                if (category.type == RE::FormType::Armor) {
                    const auto* armor = skyrim_cast<RE::TESObjectARMO*>(form);
                    const bool isJewelry = IsJewelry(armor);
                    if (std::string_view(category.name) == "Jewelry" ? !isJewelry : isJewelry)
                        continue;
                }
                ++records;
                auto* weightForm = skyrim_cast<RE::TESWeightForm*>(form);
                if (!weightForm) {
                    ++unsupported;
                    if (debugLog) {
                        log << "  UNSUPPORTED FormID=" << std::hex << std::uppercase
                            << form->GetFormID() << std::dec << '\n';
                    }
                    continue;
                }
                ++supported;
                if (weightForm->weight == category.weight) {
                    ++unchanged;
                    continue;
                }
                if (debugLog) {
                    log << "  CHANGED FormID=" << std::hex << std::uppercase << form->GetFormID()
                        << std::dec << " " << weightForm->weight << " -> " << category.weight << '\n';
                }
                weightForm->weight = category.weight;
                ++changed;
            }

            totalRecords += records;
            totalSupported += supported;
            totalChanged += changed;
            totalUnchanged += unchanged;
            totalUnsupported += unsupported;
            log << "  Records: " << records << ", Supported: " << supported
                << ", Changed: " << changed << ", Unchanged: " << unchanged
                << ", Unsupported: " << unsupported << "\n";
        }

        log << "\n================================\n"
            << "APPLY COMPLETE\n"
            << "Enabled categories: " << enabledCategories << '\n'
            << "Skipped categories: " << skippedCategories << '\n'
            << "Records scanned: " << totalRecords << '\n'
            << "Weight supported: " << totalSupported << '\n'
            << "Weights changed: " << totalChanged << '\n'
            << "Weights unchanged: " << totalUnchanged << '\n'
            << "Unsupported records: " << totalUnsupported << '\n';
        // Disabled categories are intentionally excluded from scan totals.
        // Armor and Jewelry are disjoint subsets of ARMO; no record is applied twice.
    }

    void OnMessage(SKSE::MessagingInterface::Message* message)
    {
        if (message && message->type == SKSE::MessagingInterface::kDataLoaded)
            ScanAndApply();
    }
}

SKSEPluginLoad(const SKSE::LoadInterface* skse)
{
    SKSE::Init(skse);
    auto* messaging = SKSE::GetMessagingInterface();
    return messaging && messaging->RegisterListener(UWS::OnMessage);
}
