#include "Settings.h"
#include "logger.h"

const char* BasePath = "Data/SKSE/Plugins/NPC Stats";
const char* NPCPath = "Data/SKSE/Plugins/NPC Stats/NPC";
const char* PresetsPath = "Data/SKSE/Plugins/NPC Stats/Presets";

namespace {
    const std::string LOC_BASE_DIR = "Data/SKSE/Plugins/NPC Stats/";
    const std::string LANG_PATH = LOC_BASE_DIR + "Language.json";

    std::unordered_map<std::string, std::string> LangMap;

    void LoadLanguage() {
        LangMap.clear();
        std::ifstream file(LANG_PATH, std::ios::binary);
        if (!file.is_open()) {
            logger::warn("[NPC Stats] Language.json not found. Using default texts.");
            return;
        }

        std::stringstream buffer;
        buffer << file.rdbuf();
        std::string jsonStr = buffer.str();
        file.close();

        // Remove BOM se existir
        if (jsonStr.size() >= 3 && (unsigned char)jsonStr[0] == 0xEF &&
            (unsigned char)jsonStr[1] == 0xBB && (unsigned char)jsonStr[2] == 0xBF) {
            jsonStr.erase(0, 3);
        }

        rapidjson::Document doc;
        doc.Parse(jsonStr.c_str());

        if (doc.HasParseError()) return;

        if (doc.IsObject()) {
            for (auto itr = doc.MemberBegin(); itr != doc.MemberEnd(); ++itr) {
                if (itr->value.IsObject()) {
                    std::string category = itr->name.GetString();
                    for (auto jtr = itr->value.MemberBegin(); jtr != itr->value.MemberEnd(); ++jtr) {
                        if (jtr->value.IsString()) {
                            LangMap[category + "." + jtr->name.GetString()] = jtr->value.GetString();
                        }
                    }
                }
                else if (itr->value.IsString()) {
                    LangMap[itr->name.GetString()] = itr->value.GetString();
                }
            }
        }
    }

    const char* GetLoc(const std::string& key, const char* defaultVal) {
        auto it = LangMap.find(key);
        if (it != LangMap.end()) return it->second.c_str();
        return defaultVal;
    }
}
// Variaveis de estado para a UI
static RE::Actor* g_currentActor = nullptr;
static RE::TESNPC* g_currentNPC = nullptr;

static bool isEditingPreset = false;
static std::string activePresetName = "";
static std::string ui_linkedPreset = "";
static std::vector<std::string> ui_availablePresets;

// Atributos base (Stats)
static float ui_health = 100.0f;
static float ui_magicka = 100.0f;
static float ui_stamina = 100.0f;

static int ui_healthOffset = 0;
static int ui_magickaOffset = 0;
static int ui_staminaOffset = 0;

static uint16_t ui_calcMinLevel = 1;
static uint16_t ui_calcMaxLevel = 999;
static float ui_levelMult = 1.0f;
static uint16_t ui_level = 1;

static uint16_t ui_speedMult = 100;
static uint16_t ui_dispositionBase = 35;
static int ui_bleedoutOverride = 0;

// Flags
static bool ui_isEssential = false;
static bool ui_isProtected = false;
static bool ui_isUnique = false;
static bool ui_calcStats = false;
static bool ui_doesntAffectStealthMeter = false;

// Multiplicadores (Actor Values Instanciados)
static float ui_attackDamageMult = 1.0f;
static float ui_healRateMult = 100.0f;
static float ui_magickaRateMult = 100.0f;
static float ui_staminaRateMult = 100.0f;

// Form Refs
static RE::TESClass* ui_class = nullptr;
static RE::TESCombatStyle* ui_combatStyle = nullptr;

// Skills
static uint8_t ui_skills[18] = { 15 };
static uint8_t ui_skillOffsets[18] = { 0 };
static const char* skillNames[18] = {
    "One-Handed", "Two-Handed", "Archery", "Block", "Smithing", "Heavy Armor",
    "Light Armor", "Pickpocket", "Lockpicking", "Sneak", "Alchemy", "Speech",
    "Alteration", "Conjuration", "Destruction", "Illusion", "Restoration", "Enchanting"
};

// Perks & Factions UI Structs
struct UIPerk {
    RE::BGSPerk* perk;
    int rank;
};
struct UIFaction {
    RE::TESFaction* faction;
    int rank;
};
static std::vector<UIPerk> ui_perks;
static std::vector<UIFaction> ui_factions;
static std::vector<RE::SpellItem*> ui_spells;
// Backups e Engine States
static rapidjson::Document originalEngineState;
static std::map<RE::FormID, std::string> g_vanillaNPCStates;
static std::string g_lastSavedStateStr = "";
static std::string g_lastSavedPresetLink = "";

// ==========================================
// FUNÇÕES AUXILIARES E JSON
// ==========================================

void CaptureVanillaState(RE::TESNPC* npc, std::string& outJson) {
    rapidjson::Document doc;
    auto& allocator = doc.GetAllocator();
    doc.SetObject();

    doc.AddMember("health", static_cast<float>(npc->playerSkills.health), allocator);
    doc.AddMember("magicka", static_cast<float>(npc->playerSkills.magicka), allocator);
    doc.AddMember("stamina", static_cast<float>(npc->playerSkills.stamina), allocator);

    doc.AddMember("healthOffset", static_cast<int>(npc->actorData.healthOffset), allocator);
    doc.AddMember("magickaOffset", static_cast<int>(npc->actorData.magickaOffset), allocator);
    doc.AddMember("staminaOffset", static_cast<int>(npc->actorData.staminaOffset), allocator);

    doc.AddMember("calcMinLevel", npc->actorData.calcLevelMin, allocator);
    doc.AddMember("calcMaxLevel", npc->actorData.calcLevelMax, allocator);
    doc.AddMember("level", npc->actorData.level, allocator);
    doc.AddMember("speedMult", npc->actorData.speedMult, allocator);
    doc.AddMember("dispositionBase", npc->actorData.baseDisposition, allocator);
    doc.AddMember("bleedoutOverride", npc->actorData.bleedoutOverride, allocator);

    auto flags = npc->actorData.actorBaseFlags;
    doc.AddMember("isEssential", flags.all(RE::ACTOR_BASE_DATA::Flag::kEssential), allocator);
    doc.AddMember("isProtected", flags.all(RE::ACTOR_BASE_DATA::Flag::kProtected), allocator);
    doc.AddMember("isUnique", flags.all(RE::ACTOR_BASE_DATA::Flag::kUnique), allocator);
    doc.AddMember("calcStats", flags.all(RE::ACTOR_BASE_DATA::Flag::kPCLevelMult), allocator);
    doc.AddMember("doesntAffectStealthMeter", flags.all(RE::ACTOR_BASE_DATA::Flag::kDoesntAffectStealthMeter), allocator); 
    float atkMult = 1.0f, healMult = 100.0f, magMult = 100.0f, stamMult = 100.0f;
    if (g_currentActor && g_currentActor->GetActorBase() == npc) {
        if (auto avOwner = g_currentActor->AsActorValueOwner()) {
            atkMult = avOwner->GetActorValue(RE::ActorValue::kAttackDamageMult);
            healMult = avOwner->GetActorValue(RE::ActorValue::kHealRateMult);
            magMult = avOwner->GetActorValue(RE::ActorValue::kMagickaRateMult);
            stamMult = avOwner->GetActorValue(RE::ActorValue::kStaminaRateMult);
        }
    }
    doc.AddMember("attackDamageMult", atkMult, allocator);
    doc.AddMember("healRateMult", healMult, allocator);
    doc.AddMember("magickaRateMult", magMult, allocator);
    doc.AddMember("staminaRateMult", stamMult, allocator);
    if (npc->npcClass) doc.AddMember("class", rapidjson::Value(FormUtil::NormalizeFormID(npc->npcClass).c_str(), allocator), allocator);
    if (npc->combatStyle) doc.AddMember("combatStyle", rapidjson::Value(FormUtil::NormalizeFormID(npc->combatStyle).c_str(), allocator), allocator);

    rapidjson::Value skillsArray(rapidjson::kArrayType);
    rapidjson::Value skillOffArray(rapidjson::kArrayType);
    for (int i = 0; i < 18; i++) {
        skillsArray.PushBack(npc->playerSkills.values[i], allocator);
        skillOffArray.PushBack(npc->playerSkills.offsets[i], allocator);
    }
    doc.AddMember("skills", skillsArray, allocator);
    doc.AddMember("skillOffsets", skillOffArray, allocator);

    rapidjson::Value perkArray(rapidjson::kArrayType);
    for (std::uint32_t i = 0; i < npc->perkCount; i++) {
        if (npc->perks && npc->perks[i].perk) {
            rapidjson::Value pObj(rapidjson::kObjectType);
            pObj.AddMember("form", rapidjson::Value(FormUtil::NormalizeFormID(npc->perks[i].perk).c_str(), allocator), allocator);
            pObj.AddMember("rank", npc->perks[i].currentRank, allocator);
            perkArray.PushBack(pObj, allocator);
        }
    }
    doc.AddMember("perks", perkArray, allocator);

    rapidjson::Value factionArray(rapidjson::kArrayType);
    for (auto& f : npc->factions) {
        if (f.faction) {
            rapidjson::Value fObj(rapidjson::kObjectType);
            fObj.AddMember("form", rapidjson::Value(FormUtil::NormalizeFormID(f.faction).c_str(), allocator), allocator);
            fObj.AddMember("rank", f.rank, allocator);
            factionArray.PushBack(fObj, allocator);
        }
    }
    doc.AddMember("factions", factionArray, allocator);

    rapidjson::Value spellArray(rapidjson::kArrayType);
    if (auto spellList = npc->GetSpellList()) {
        for (std::uint32_t i = 0; i < spellList->numSpells; i++) {
            if (spellList->spells && spellList->spells[i]) {
                spellArray.PushBack(rapidjson::Value(FormUtil::NormalizeFormID(spellList->spells[i]).c_str(), allocator), allocator);
            }
        }
    }
    doc.AddMember("spells", spellArray, allocator);

    rapidjson::StringBuffer buffer;
    rapidjson::PrettyWriter<rapidjson::StringBuffer> writer(buffer);
    doc.Accept(writer);
    outJson = buffer.GetString();
}

void GenerateJSONFromUI(rapidjson::Document& doc) {
    auto& allocator = doc.GetAllocator();
    doc.SetObject();

    doc.AddMember("health", ui_health, allocator);
    doc.AddMember("magicka", ui_magicka, allocator);
    doc.AddMember("stamina", ui_stamina, allocator);

    doc.AddMember("healthOffset", ui_healthOffset, allocator);
    doc.AddMember("magickaOffset", ui_magickaOffset, allocator);
    doc.AddMember("staminaOffset", ui_staminaOffset, allocator);

    doc.AddMember("calcMinLevel", ui_calcMinLevel, allocator);
    doc.AddMember("calcMaxLevel", ui_calcMaxLevel, allocator);
    if (ui_calcStats) doc.AddMember("level", static_cast<uint16_t>(ui_levelMult * 1000.0f), allocator);
    else doc.AddMember("level", ui_level, allocator);

    doc.AddMember("speedMult", ui_speedMult, allocator);
    doc.AddMember("dispositionBase", ui_dispositionBase, allocator);
    doc.AddMember("bleedoutOverride", ui_bleedoutOverride, allocator);

    doc.AddMember("isEssential", ui_isEssential, allocator);
    doc.AddMember("isProtected", ui_isProtected, allocator);
    doc.AddMember("isUnique", ui_isUnique, allocator);
    doc.AddMember("calcStats", ui_calcStats, allocator);
    doc.AddMember("doesntAffectStealthMeter", ui_doesntAffectStealthMeter, allocator);
    doc.AddMember("attackDamageMult", ui_attackDamageMult, allocator);
    doc.AddMember("healRateMult", ui_healRateMult, allocator);
    doc.AddMember("magickaRateMult", ui_magickaRateMult, allocator);
    doc.AddMember("staminaRateMult", ui_staminaRateMult, allocator);

    if (ui_class) doc.AddMember("class", rapidjson::Value(FormUtil::NormalizeFormID(ui_class).c_str(), allocator), allocator);
    if (ui_combatStyle) doc.AddMember("combatStyle", rapidjson::Value(FormUtil::NormalizeFormID(ui_combatStyle).c_str(), allocator), allocator);

    rapidjson::Value skillsArray(rapidjson::kArrayType);
    rapidjson::Value skillOffArray(rapidjson::kArrayType);
    for (int i = 0; i < 18; i++) {
        skillsArray.PushBack(ui_skills[i], allocator);
        skillOffArray.PushBack(ui_skillOffsets[i], allocator);
    }
    doc.AddMember("skills", skillsArray, allocator);
    doc.AddMember("skillOffsets", skillOffArray, allocator);

    rapidjson::Value perkArray(rapidjson::kArrayType);
    for (const auto& p : ui_perks) {
        if (p.perk) {
            rapidjson::Value pObj(rapidjson::kObjectType);
            pObj.AddMember("form", rapidjson::Value(FormUtil::NormalizeFormID(p.perk).c_str(), allocator), allocator);
            pObj.AddMember("rank", p.rank, allocator);
            perkArray.PushBack(pObj, allocator);
        }
    }
    doc.AddMember("perks", perkArray, allocator);

    rapidjson::Value factionArray(rapidjson::kArrayType);
    for (const auto& f : ui_factions) {
        if (f.faction) {
            rapidjson::Value fObj(rapidjson::kObjectType);
            fObj.AddMember("form", rapidjson::Value(FormUtil::NormalizeFormID(f.faction).c_str(), allocator), allocator);
            fObj.AddMember("rank", f.rank, allocator);
            factionArray.PushBack(fObj, allocator);
        }
    }
    doc.AddMember("factions", factionArray, allocator);

    rapidjson::Value spellArray(rapidjson::kArrayType);
    for (auto* sp : ui_spells) {
        if (sp) spellArray.PushBack(rapidjson::Value(FormUtil::NormalizeFormID(sp).c_str(), allocator), allocator);
    }
    doc.AddMember("spells", spellArray, allocator);
}

void ParseJSONToUI(const rapidjson::Document& j) {
    ui_class = nullptr;
    ui_combatStyle = nullptr;
    ui_perks.clear();
    ui_factions.clear();
    ui_spells.clear();

    if (j.HasMember("health") && j["health"].IsFloat()) ui_health = j["health"].GetFloat();
    if (j.HasMember("magicka") && j["magicka"].IsFloat()) ui_magicka = j["magicka"].GetFloat();
    if (j.HasMember("stamina") && j["stamina"].IsFloat()) ui_stamina = j["stamina"].GetFloat();

    if (j.HasMember("healthOffset") && j["healthOffset"].IsInt()) ui_healthOffset = j["healthOffset"].GetInt();
    if (j.HasMember("magickaOffset") && j["magickaOffset"].IsInt()) ui_magickaOffset = j["magickaOffset"].GetInt();
    if (j.HasMember("staminaOffset") && j["staminaOffset"].IsInt()) ui_staminaOffset = j["staminaOffset"].GetInt();

    if (j.HasMember("calcMinLevel") && j["calcMinLevel"].IsUint()) ui_calcMinLevel = static_cast<uint16_t>(j["calcMinLevel"].GetUint());
    if (j.HasMember("calcMaxLevel") && j["calcMaxLevel"].IsUint()) ui_calcMaxLevel = static_cast<uint16_t>(j["calcMaxLevel"].GetUint());
    if (j.HasMember("speedMult") && j["speedMult"].IsUint()) ui_speedMult = static_cast<uint16_t>(j["speedMult"].GetUint());
    if (j.HasMember("dispositionBase") && j["dispositionBase"].IsUint()) ui_dispositionBase = static_cast<uint16_t>(j["dispositionBase"].GetUint());
    if (j.HasMember("bleedoutOverride") && j["bleedoutOverride"].IsInt()) ui_bleedoutOverride = j["bleedoutOverride"].GetInt();

    if (j.HasMember("isEssential") && j["isEssential"].IsBool()) ui_isEssential = j["isEssential"].GetBool();
    if (j.HasMember("isProtected") && j["isProtected"].IsBool()) ui_isProtected = j["isProtected"].GetBool();
    if (j.HasMember("isUnique") && j["isUnique"].IsBool()) ui_isUnique = j["isUnique"].GetBool();
    if (j.HasMember("calcStats") && j["calcStats"].IsBool()) ui_calcStats = j["calcStats"].GetBool();
    if (j.HasMember("doesntAffectStealthMeter") && j["doesntAffectStealthMeter"].IsBool()) ui_doesntAffectStealthMeter = j["doesntAffectStealthMeter"].GetBool(); 
    if (j.HasMember("attackDamageMult") && j["attackDamageMult"].IsFloat()) ui_attackDamageMult = j["attackDamageMult"].GetFloat();
    if (j.HasMember("healRateMult") && j["healRateMult"].IsFloat()) ui_healRateMult = j["healRateMult"].GetFloat();
    if (j.HasMember("magickaRateMult") && j["magickaRateMult"].IsFloat()) ui_magickaRateMult = j["magickaRateMult"].GetFloat();
    if (j.HasMember("staminaRateMult") && j["staminaRateMult"].IsFloat()) ui_staminaRateMult = j["staminaRateMult"].GetFloat();
    if (j.HasMember("level") && j["level"].IsUint()) {
        uint16_t lvl = static_cast<uint16_t>(j["level"].GetUint());
        if (ui_calcStats) ui_levelMult = lvl / 1000.0f;
        else ui_level = lvl;
    }

    if (j.HasMember("class")) ui_class = RE::TESForm::LookupByID<RE::TESClass>(FormUtil::FormIDFromString(j["class"].GetString()));
    if (j.HasMember("combatStyle")) ui_combatStyle = RE::TESForm::LookupByID<RE::TESCombatStyle>(FormUtil::FormIDFromString(j["combatStyle"].GetString()));

    if (j.HasMember("skills") && j["skills"].IsArray()) {
        auto sArray = j["skills"].GetArray();
        for (rapidjson::SizeType i = 0; i < sArray.Size() && i < 18; i++) ui_skills[i] = static_cast<uint8_t>(sArray[i].GetInt());
    }
    if (j.HasMember("skillOffsets") && j["skillOffsets"].IsArray()) {
        auto sArray = j["skillOffsets"].GetArray();
        for (rapidjson::SizeType i = 0; i < sArray.Size() && i < 18; i++) ui_skillOffsets[i] = static_cast<uint8_t>(sArray[i].GetInt());
    }

    if (j.HasMember("perks") && j["perks"].IsArray()) {
        for (auto& p : j["perks"].GetArray()) {
            if (p.HasMember("form") && p.HasMember("rank")) {
                if (auto perk = RE::TESForm::LookupByID<RE::BGSPerk>(FormUtil::FormIDFromString(p["form"].GetString()))) {
                    ui_perks.push_back({ perk, p["rank"].GetInt() });
                }
            }
        }
    }

    if (j.HasMember("factions") && j["factions"].IsArray()) {
        for (auto& f : j["factions"].GetArray()) {
            if (f.HasMember("form") && f.HasMember("rank")) {
                if (auto fac = RE::TESForm::LookupByID<RE::TESFaction>(FormUtil::FormIDFromString(f["form"].GetString()))) {
                    ui_factions.push_back({ fac, f["rank"].GetInt() });
                }
            }
        }
    }

    if (j.HasMember("spells") && j["spells"].IsArray()) {
        for (auto& s : j["spells"].GetArray()) {
            if (auto sp = RE::TESForm::LookupByID<RE::SpellItem>(FormUtil::FormIDFromString(s.GetString()))) {
                ui_spells.push_back(sp);
            }
        }
    }

}

void UpdateLastSavedState() {
    rapidjson::Document doc;
    GenerateJSONFromUI(doc);
    rapidjson::StringBuffer buffer;
    rapidjson::PrettyWriter<rapidjson::StringBuffer> writer(buffer);
    doc.Accept(writer);
    g_lastSavedStateStr = buffer.GetString();
    g_lastSavedPresetLink = ui_linkedPreset;
}

bool HasUnsavedChanges() {
    if (ui_linkedPreset != g_lastSavedPresetLink) return true;
    rapidjson::Document doc;
    GenerateJSONFromUI(doc);
    rapidjson::StringBuffer buffer;
    rapidjson::PrettyWriter<rapidjson::StringBuffer> writer(buffer);
    doc.Accept(writer);
    return g_lastSavedStateStr != buffer.GetString();
}

// ==========================================
// EXPORTAÇÃO
// ==========================================
std::string SanitizeFilename(std::string name) {
    std::string invalid = "<>:/\\|?*\"";
    for (char& c : name) { if (invalid.find(c) != std::string::npos) c = '_'; }
    return name;
}

void ExportPresetAsZip(const std::string& presetName) {
    namespace fs = std::filesystem;
    std::string sourcePath = std::format("{}/{}.json", PresetsPath, presetName);
    if (!fs::exists(sourcePath)) return;

    fs::path exportDir = "Data/Exports";
    fs::create_directories(exportDir);
    std::string zipPath = (exportDir / (SanitizeFilename(presetName) + "_StatsPreset.zip")).string();

    mz_zip_archive zip_archive;
    memset(&zip_archive, 0, sizeof(zip_archive));
    if (!mz_zip_writer_init_file(&zip_archive, zipPath.c_str(), 0)) return;

    std::string internalZipPath = std::format("SKSE/Plugins/NPC Stats Replacer/Presets/{}.json", presetName);
    mz_zip_writer_add_file(&zip_archive, internalZipPath.c_str(), sourcePath.c_str(), nullptr, 0, MZ_BEST_COMPRESSION);

    mz_zip_writer_finalize_archive(&zip_archive);
    mz_zip_writer_end(&zip_archive);
    logger::info("Preset '{}' successfully exported to: {}", presetName, zipPath);
}

void ExportNPCAsZip(RE::TESNPC* npc, const std::string& linkedPreset) {
    if (!npc) return;
    namespace fs = std::filesystem;
    std::string editorID = clib_util::editorID::get_editorID(npc);
    if (editorID.empty()) editorID = std::format("{:08X}", npc->GetFormID());
    std::string npcSourcePath = std::format("{}/{}.json", NPCPath, editorID);
    if (!fs::exists(npcSourcePath)) return;

    fs::path exportDir = "Data/Exports";
    fs::create_directories(exportDir);
    std::string npcName = npc->GetFullName() ? npc->GetFullName() : editorID;
    std::string zipPath = (exportDir / (SanitizeFilename(npcName) + "_NPCStats.zip")).string();

    mz_zip_archive zip_archive;
    memset(&zip_archive, 0, sizeof(zip_archive));
    if (!mz_zip_writer_init_file(&zip_archive, zipPath.c_str(), 0)) return;

    std::string internalNpcPath = std::format("SKSE/Plugins/NPC Stats Replacer/NPC/{}.json", editorID);
    mz_zip_writer_add_file(&zip_archive, internalNpcPath.c_str(), npcSourcePath.c_str(), nullptr, 0, MZ_BEST_COMPRESSION);

    if (!linkedPreset.empty()) {
        std::string presetSourcePath = std::format("{}/{}.json", PresetsPath, linkedPreset);
        if (fs::exists(presetSourcePath)) {
            std::string internalPresetPath = std::format("SKSE/Plugins/NPC Stats Replacer/Presets/{}.json", linkedPreset);
            mz_zip_writer_add_file(&zip_archive, internalPresetPath.c_str(), presetSourcePath.c_str(), nullptr, 0, MZ_BEST_COMPRESSION);
        }
    }

    mz_zip_writer_finalize_archive(&zip_archive);
    mz_zip_writer_end(&zip_archive);
    logger::info("NPC '{}' successfully exported.", editorID);
}

void RefreshAvailablePresets() {
    ui_availablePresets.clear();
    std::filesystem::create_directories(PresetsPath);
    for (const auto& entry : std::filesystem::directory_iterator(PresetsPath)) {
        if (entry.path().extension() == ".json") {
            ui_availablePresets.push_back(entry.path().stem().string());
        }
    }
}

// ==========================================
// UI DROPDOWN
// ==========================================
template <typename T>
void DrawDropdown(const char* label, const std::string& category, T** formPtr, int& selectedIndex, bool disabled = false, float customWidth = -1.0f) {
    const auto& fullList = Manager::GetSingleton()->GetList(category);
    if (fullList.empty()) return;

    std::vector<const char*> comboItems;
    std::vector<int> mapToFull;

    comboItems.push_back(GetLoc("text.none", "None"));
    mapToFull.push_back(-1);

    for (size_t i = 0; i < fullList.size(); ++i) {
        comboItems.push_back(fullList[i].cachedDisplayName.c_str());
        mapToFull.push_back(static_cast<int>(i));
    }

    int localSelection = 0;
    if (*formPtr) {
        RE::FormID targetID = (*formPtr)->GetFormID();
        for (size_t i = 1; i < mapToFull.size(); i++) {
            if (fullList[mapToFull[i]].formID == targetID) {
                localSelection = static_cast<int>(i);
                break;
            }
        }
    }

    ImGuiMCP::PushID(label);
    std::string displayLabel = label;
    size_t hashPos = displayLabel.find("##");
    if (hashPos != std::string::npos) displayLabel = displayLabel.substr(0, hashPos);

    ImGuiMCP::Text("%s:", displayLabel.c_str());
    ImGuiMCP::SameLine();

    if (disabled) {
        ImGuiMCP::TextColored({ 0.5f, 0.5f, 0.5f, 1.0f }, "%s %s", GetLoc("text.locked", "[LOCKED]"), comboItems.empty() ? GetLoc("text.none", "None") : comboItems[localSelection]);
    }
    else {
        if (customWidth > 0.0f) ImGuiMCP::SetNextItemWidth(customWidth);
        const char* previewValue = comboItems.empty() ? GetLoc("text.none", "None") : comboItems[localSelection];

        if (ImGuiMCP::BeginCombo("##drop", previewValue)) {
            static std::map<std::string, std::string> searchBuffers;
            char searchBuf[256] = "";
            if (searchBuffers.contains(label)) strcpy_s(searchBuf, searchBuffers[label].c_str());

            ImGuiMCP::SetNextItemWidth(-1.0f);
            if (ImGuiMCP::InputText("##busca", searchBuf, sizeof(searchBuf))) {
                searchBuffers[label] = searchBuf;
            }
            ImGuiMCP::Separator();

            std::string searchStr = searchBuf;
            std::transform(searchStr.begin(), searchStr.end(), searchStr.begin(), [](unsigned char c) { return std::tolower(c); });

            ImGuiMCP::BeginChild("##scroll", ImGuiMCP::ImVec2(0, 200), false);
            for (int i = 0; i < comboItems.size(); i++) {
                std::string itemStr = comboItems[i];
                std::string itemLower = itemStr;
                std::transform(itemLower.begin(), itemLower.end(), itemLower.begin(), [](unsigned char c) { return std::tolower(c); });

                if (searchStr.empty() || itemLower.find(searchStr) != std::string::npos) {
                    bool isSelected = (localSelection == i);
                    if (ImGuiMCP::Selectable(comboItems[i], isSelected)) {
                        localSelection = i;
                        int originalIndex = mapToFull[localSelection];
                        if (originalIndex == -1) *formPtr = nullptr;
                        else *formPtr = RE::TESForm::LookupByID<T>(fullList[originalIndex].formID);
                        searchBuffers[label] = "";
                    }
                    if (isSelected) ImGuiMCP::SetItemDefaultFocus();
                }
            }
            ImGuiMCP::EndChild();
            ImGuiMCP::EndCombo();
        }
    }
    ImGuiMCP::PopID();
}


// ==========================================
// CARREGAMENTO E APLICAÇÃO
// ==========================================
void LoadPresetToUI(const std::string& presetName) {
    isEditingPreset = true;
    activePresetName = presetName;
    ui_linkedPreset = "";

    std::string path = std::format("{}/{}.json", PresetsPath, presetName);
    FILE* fp = nullptr;
    fopen_s(&fp, path.c_str(), "rb");
    if (fp) {
        char readBuffer[65536];
        rapidjson::FileReadStream is(fp, readBuffer, sizeof(readBuffer));
        rapidjson::Document doc;
        doc.ParseStream(is);
        fclose(fp);
        ParseJSONToUI(doc);
    }
    UpdateLastSavedState();
}

void LoadNPCToUI(RE::TESNPC* npcToLoad = nullptr, RE::Actor* actorRef = nullptr) {
    if (!npcToLoad) {
        auto ref = RE::Console::GetSelectedRef();
        if (!ref) return;
        g_currentActor = ref->As<RE::Actor>();
        if (!g_currentActor) return;
        g_currentNPC = g_currentActor->GetActorBase();
    }
    else {
        g_currentNPC = npcToLoad;
        g_currentActor = actorRef;
    }

    if (!g_currentNPC) return;

    if (!g_vanillaNPCStates.contains(g_currentNPC->GetFormID())) {
        std::string vanillaStr;
        CaptureVanillaState(g_currentNPC, vanillaStr);
        g_vanillaNPCStates[g_currentNPC->GetFormID()] = vanillaStr;
    }

    isEditingPreset = false;
    activePresetName = "";
    ui_linkedPreset = "";

    // Extrai Base e Offsets
    ui_health = static_cast<float>(g_currentNPC->playerSkills.health);
    ui_magicka = static_cast<float>(g_currentNPC->playerSkills.magicka);
    ui_stamina = static_cast<float>(g_currentNPC->playerSkills.stamina);

    ui_healthOffset = g_currentNPC->actorData.healthOffset;
    ui_magickaOffset = g_currentNPC->actorData.magickaOffset;
    ui_staminaOffset = g_currentNPC->actorData.staminaOffset;

    ui_calcMinLevel = g_currentNPC->actorData.calcLevelMin;
    ui_calcMaxLevel = g_currentNPC->actorData.calcLevelMax;
    ui_speedMult = g_currentNPC->actorData.speedMult;
    ui_dispositionBase = g_currentNPC->actorData.baseDisposition;
    ui_bleedoutOverride = g_currentNPC->actorData.bleedoutOverride;

    auto flags = g_currentNPC->actorData.actorBaseFlags;
    ui_isEssential = flags.all(RE::ACTOR_BASE_DATA::Flag::kEssential);
    ui_isProtected = flags.all(RE::ACTOR_BASE_DATA::Flag::kProtected);
    ui_isUnique = flags.all(RE::ACTOR_BASE_DATA::Flag::kUnique);
    ui_calcStats = flags.all(RE::ACTOR_BASE_DATA::Flag::kPCLevelMult);
    ui_doesntAffectStealthMeter = flags.all(RE::ACTOR_BASE_DATA::Flag::kDoesntAffectStealthMeter);
    if (ui_calcStats) ui_levelMult = g_currentNPC->actorData.level / 1000.0f;
    else ui_level = g_currentNPC->actorData.level;

    ui_class = g_currentNPC->npcClass;
    ui_combatStyle = g_currentNPC->combatStyle;

    ui_attackDamageMult = 1.0f; 
    ui_healRateMult = 100.0f; 
    ui_magickaRateMult = 100.0f; 
    ui_staminaRateMult = 100.0f;

    if (g_currentActor) {
        if (auto avOwner = g_currentActor->AsActorValueOwner()) {
            ui_attackDamageMult = avOwner->GetActorValue(RE::ActorValue::kAttackDamageMult);
            ui_healRateMult = avOwner->GetActorValue(RE::ActorValue::kHealRateMult);
            ui_magickaRateMult = avOwner->GetActorValue(RE::ActorValue::kMagickaRateMult);
            ui_staminaRateMult = avOwner->GetActorValue(RE::ActorValue::kStaminaRateMult);
        }
    }

    for (int i = 0; i < 18; i++) {
        ui_skills[i] = g_currentNPC->playerSkills.values[i];
        ui_skillOffsets[i] = g_currentNPC->playerSkills.offsets[i];
    }

    ui_perks.clear();
    for (std::uint32_t i = 0; i < g_currentNPC->perkCount; i++) {
        if (g_currentNPC->perks && g_currentNPC->perks[i].perk) ui_perks.push_back({ g_currentNPC->perks[i].perk, g_currentNPC->perks[i].currentRank });
    }

    ui_factions.clear();
    for (auto& f : g_currentNPC->factions) {
        if (f.faction) ui_factions.push_back({ f.faction, f.rank });
    }

    ui_spells.clear();
    if (auto spellList = g_currentNPC->GetSpellList()) {
        for (std::uint32_t i = 0; i < spellList->numSpells; i++) {
            if (spellList->spells && spellList->spells[i]) ui_spells.push_back(spellList->spells[i]);
        }
    }

    GenerateJSONFromUI(originalEngineState);

    std::string editorID = clib_util::editorID::get_editorID(g_currentNPC);
    if (editorID.empty()) editorID = std::format("{:08X}", g_currentNPC->GetFormID());
    std::string filePath = std::format("{}/{}.json", NPCPath, editorID);

    if (std::filesystem::exists(filePath)) {
        FILE* fp = nullptr;
        fopen_s(&fp, filePath.c_str(), "rb");
        if (fp) {
            char readBuffer[65536];
            rapidjson::FileReadStream is(fp, readBuffer, sizeof(readBuffer));
            rapidjson::Document doc;
            doc.ParseStream(is);
            fclose(fp);

            if (doc.IsObject()) {
                if (doc.HasMember("preset") && doc["preset"].IsString()) {
                    ui_linkedPreset = doc["preset"].GetString();
                    std::string pPath = std::format("{}/{}.json", PresetsPath, ui_linkedPreset);
                    FILE* pFp = nullptr;
                    fopen_s(&pFp, pPath.c_str(), "rb");
                    if (pFp) {
                        char pReadBuffer[65536];
                        rapidjson::FileReadStream pIs(pFp, pReadBuffer, sizeof(pReadBuffer));
                        rapidjson::Document pDoc;
                        pDoc.ParseStream(pIs);
                        fclose(pFp);
                        ParseJSONToUI(pDoc);
                    }
                }
                else {
                    ParseJSONToUI(doc);
                }
            }
        }
    }
    UpdateLastSavedState();
}
void SaveData() {
    rapidjson::Document doc;
    auto& allocator = doc.GetAllocator();
    doc.SetObject();
    std::string finalPath;

    if (isEditingPreset) {
        GenerateJSONFromUI(doc);
        std::filesystem::create_directories(PresetsPath);
        finalPath = std::format("{}/{}.json", PresetsPath, activePresetName);
    }
    else {
        if (!g_currentNPC) return;
        if (!ui_linkedPreset.empty()) {
            doc.AddMember("preset", rapidjson::Value(ui_linkedPreset.c_str(), allocator), allocator);
        }
        else {
            GenerateJSONFromUI(doc);
        }
        std::string editorID = clib_util::editorID::get_editorID(g_currentNPC);
        if (editorID.empty()) editorID = std::format("{:08X}", g_currentNPC->GetFormID());
        std::filesystem::create_directories(NPCPath);
        finalPath = std::format("{}/{}.json", NPCPath, editorID);
    }

    FILE* fp = nullptr;
    fopen_s(&fp, finalPath.c_str(), "wb");
    if (fp) {
        char writeBuffer[65536];
        rapidjson::FileWriteStream os(fp, writeBuffer, sizeof(writeBuffer));
        rapidjson::PrettyWriter<rapidjson::FileWriteStream> writer(os);
        doc.Accept(writer);
        fclose(fp);
        logger::info("Stats saved to {}", finalPath);
    }

    // Atualiza automaticamente os NPCs caso seja um preset
    if (isEditingPreset && !activePresetName.empty() && std::filesystem::exists(NPCPath)) {
        for (const auto& entry : std::filesystem::directory_iterator(NPCPath)) {
            if (entry.path().extension() == ".json") {
                FILE* nFp = nullptr;
                fopen_s(&nFp, entry.path().string().c_str(), "rb");
                if (nFp) {
                    char readBuffer[2048];
                    rapidjson::FileReadStream is(nFp, readBuffer, sizeof(readBuffer));
                    rapidjson::Document npcDoc;
                    npcDoc.ParseStream(is);
                    fclose(nFp);

                    if (npcDoc.IsObject() && npcDoc.HasMember("preset") && npcDoc["preset"].GetString() == activePresetName) {
                        std::string filename = entry.path().stem().string();
                        RE::TESNPC* targetNPC = nullptr;
                        if (auto edidForm = RE::TESForm::LookupByEditorID(filename)) targetNPC = edidForm->As<RE::TESNPC>();
                        else {
                            try { RE::FormID id = std::stoul(filename, nullptr, 16); if (auto f = RE::TESForm::LookupByID(id)) targetNPC = f->As<RE::TESNPC>(); }
                            catch (...) {}
                        }
                        if (targetNPC) {
                            Manager::ApplyNPCCustomizationFromJSON(targetNPC, doc);
                        }
                    }
                }
            }
        }
    }
    UpdateLastSavedState();
}

void ApplyNPC() {
    if (!g_currentNPC) return;
    rapidjson::Document doc;

    if (!ui_linkedPreset.empty()) {
        std::string pPath = std::format("{}/{}.json", PresetsPath, ui_linkedPreset);
        FILE* fp = nullptr;
        fopen_s(&fp, pPath.c_str(), "rb");
        if (fp) {
            char readBuffer[65536];
            rapidjson::FileReadStream is(fp, readBuffer, sizeof(readBuffer));
            doc.ParseStream(is);
            fclose(fp);
        }
    }
    else {
        GenerateJSONFromUI(doc);
    }

    Manager::ApplyNPCCustomizationFromJSON(g_currentNPC, doc);

    // Se houver um Ator carregado no mundo e a vida mudar, atualizamos instantaneamente
    if (g_currentActor) {
        auto avOwner = g_currentActor->AsActorValueOwner();
        if (avOwner) {
            avOwner->SetBaseActorValue(RE::ActorValue::kHealth, ui_health);
            avOwner->SetBaseActorValue(RE::ActorValue::kMagicka, ui_magicka);
            avOwner->SetBaseActorValue(RE::ActorValue::kStamina, ui_stamina);
            avOwner->SetActorValue(RE::ActorValue::kAttackDamageMult, ui_attackDamageMult);
            avOwner->SetActorValue(RE::ActorValue::kHealRateMult, ui_healRateMult);
            avOwner->SetActorValue(RE::ActorValue::kMagickaRateMult, ui_magickaRateMult);
            avOwner->SetActorValue(RE::ActorValue::kStaminaRateMult, ui_staminaRateMult);
            // Aplica o valor de cada uma das 18 Skills no ator em cena
            // No Skyrim as ActorValues de skills são contínuas do ID 16 (OneHanded) até 33 (Enchanting)
            const RE::ActorValue SkillToActorValue[18] = {
                    RE::ActorValue::kOneHanded, RE::ActorValue::kTwoHanded, RE::ActorValue::kArchery, RE::ActorValue::kBlock,
                    RE::ActorValue::kSmithing, RE::ActorValue::kHeavyArmor, RE::ActorValue::kLightArmor, RE::ActorValue::kPickpocket,
                    RE::ActorValue::kLockpicking, RE::ActorValue::kSneak, RE::ActorValue::kAlchemy, RE::ActorValue::kSpeech,
                    RE::ActorValue::kAlteration, RE::ActorValue::kConjuration, RE::ActorValue::kDestruction, RE::ActorValue::kIllusion,
                    RE::ActorValue::kRestoration, RE::ActorValue::kEnchanting
            };

            for (int i = 0; i < 18; i++) {
                avOwner->SetBaseActorValue(SkillToActorValue[i], ui_skills[i]);
            }
        }

        g_currentActor->EvaluatePackage(false, true); // Força refresh da IA / stats
    }
}

void RevertNPC() {
    if (!g_currentNPC || !originalEngineState.IsObject()) return;
    ui_linkedPreset = "";
    Manager::ApplyNPCCustomizationFromJSON(g_currentNPC, originalEngineState);
    LoadNPCToUI(g_currentNPC, g_currentActor);
    if (g_currentActor) g_currentActor->EvaluatePackage(false, true);
}

void RestoreDefaultNPC() {
    if (!g_currentNPC) return;
    std::string editorID = clib_util::editorID::get_editorID(g_currentNPC);
    if (editorID.empty()) editorID = std::format("{:08X}", g_currentNPC->GetFormID());
    std::string filePath = std::format("{}/{}.json", NPCPath, editorID);

    if (std::filesystem::exists(filePath)) std::filesystem::remove(filePath);
    ui_linkedPreset = "";

    if (g_vanillaNPCStates.contains(g_currentNPC->GetFormID())) {
        rapidjson::Document doc;
        doc.Parse(g_vanillaNPCStates[g_currentNPC->GetFormID()].c_str());
        Manager::ApplyNPCCustomizationFromJSON(g_currentNPC, doc);
    }
    else {
        Manager::ApplyNPCCustomizationFromJSON(g_currentNPC, originalEngineState);
    }
    LoadNPCToUI(g_currentNPC, g_currentActor);
    if (g_currentActor) g_currentActor->EvaluatePackage(false, true);
}



// ==========================================
// MENUS UI PRINCIPAIS
// ==========================================
void DrawMainEditorUI() {
    bool isLocked = (!ui_linkedPreset.empty() && !isEditingPreset);
    bool isDirty = HasUnsavedChanges();

    if (!isEditingPreset && ImGuiMCP::Button(GetLoc("btn.apply", "Apply in-game"))) { ApplyNPC(); }
    ImGuiMCP::SameLine();
    if (isDirty) ImGuiMCP::PushStyleColor(ImGuiMCP::ImGuiCol_Button, { 0.6f, 0.4f, 0.1f, 1.0f });
    if (ImGuiMCP::Button(GetLoc("btn.save", "Save data"))) { SaveData(); }
    if (isDirty) ImGuiMCP::PopStyleColor();

    if (!isEditingPreset) {
        ImGuiMCP::SameLine();
        if (ImGuiMCP::Button(GetLoc("btn.undo", "Undo Changes"))) { RevertNPC(); }
        ImGuiMCP::SameLine();
        if (ImGuiMCP::Button(GetLoc("btn.restore", "Restore Default"))) { RestoreDefaultNPC(); }
        if (g_currentNPC) {
            std::string edid = clib_util::editorID::get_editorID(g_currentNPC);
            if (edid.empty()) edid = std::format("{:08X}", g_currentNPC->GetFormID());
            if (std::filesystem::exists(std::format("{}/{}.json", NPCPath, edid))) {
                ImGuiMCP::SameLine();
                if (isDirty) ImGuiMCP::TextColored({ 0.6f, 0.6f, 0.6f, 1.0f }, "%s", GetLoc("text.save_to_export", "[Save to Export]"));
                else { if (ImGuiMCP::Button(GetLoc("btn.export", "Export"))) ExportNPCAsZip(g_currentNPC, ui_linkedPreset); }
            }
        }
    }
    else if (isEditingPreset && !activePresetName.empty()) {
        std::string presetPath = std::format("{}/{}.json", PresetsPath, activePresetName);
        if (std::filesystem::exists(presetPath)) {
            ImGuiMCP::SameLine();
            if (isDirty) ImGuiMCP::TextColored({ 0.6f, 0.6f, 0.6f, 1.0f }, "%s", GetLoc("text.save_to_export", "[Save to Export]"));
            else if (ImGuiMCP::Button(GetLoc("btn.export", "Export"))) ExportPresetAsZip(activePresetName);
        }
    }
    ImGuiMCP::Separator();

    if (isLocked) {
        ImGuiMCP::TextColored({ 1.0f, 0.5f, 0.0f, 1.0f }, GetLoc("text.locked_preset", "This NPC is locked and using PRESET: %s"), ui_linkedPreset.c_str());
        if (ImGuiMCP::Button(GetLoc("btn.unlink", "Unlink Preset (Free Edit)"))) ui_linkedPreset = "";
        ImGuiMCP::SameLine();
        if (ImGuiMCP::Button(GetLoc("btn.edit_preset", "Edit this Preset"))) LoadPresetToUI(ui_linkedPreset);
        ImGuiMCP::Separator();
    }
    else if (!isEditingPreset) {
        RefreshAvailablePresets();
        static char newPresetName[128] = "";
        static bool nameExistsError = false;

        ImGuiMCP::SetNextItemWidth(250.0f);
        ImGuiMCP::InputText(GetLoc("ui.new_preset", "New Preset Name"), newPresetName, sizeof(newPresetName));
        ImGuiMCP::SameLine();
        if (ImGuiMCP::Button(GetLoc("btn.save_preset", "Save as Preset"))) {
            if (strlen(newPresetName) > 0) {
                std::string newName(newPresetName);
                if (std::find(ui_availablePresets.begin(), ui_availablePresets.end(), newName) != ui_availablePresets.end()) {
                    nameExistsError = true;
                }
                else {
                    nameExistsError = false;
                    std::string backupName = activePresetName;
                    bool backupEdit = isEditingPreset;

                    activePresetName = newPresetName;
                    isEditingPreset = true;
                    SaveData();

                    ui_linkedPreset = newPresetName;
                    isEditingPreset = false;
                    activePresetName = "";
                    SaveData();

                    isEditingPreset = backupEdit;
                    activePresetName = backupName;
                    memset(newPresetName, 0, sizeof(newPresetName));
                }
            }
        }
        if (nameExistsError) ImGuiMCP::TextColored({ 1.0f, 0.2f, 0.2f, 1.0f }, "%s", GetLoc("text.preset_exists", "Error: A Preset with this name already exists!"));

        if (!ui_availablePresets.empty()) {
            ImGuiMCP::Text("%s", GetLoc("text.apply_existing", "Apply Existing Preset:")); ImGuiMCP::SameLine();
            static int s_presetIdx = 0;
            std::vector<const char*> presetCStrs;
            for (const auto& p : ui_availablePresets) presetCStrs.push_back(p.c_str());
            ImGuiMCP::SetNextItemWidth(200.0f);
            ImGuiMCP::Combo("##PresetCombo", &s_presetIdx, presetCStrs.data(), static_cast<int>(presetCStrs.size()));
            ImGuiMCP::SameLine();
            if (ImGuiMCP::Button(GetLoc("btn.apply_preset", "Apply"))) {
                ui_linkedPreset = ui_availablePresets[s_presetIdx];
                std::string pPath = std::format("{}/{}.json", PresetsPath, ui_linkedPreset);
                FILE* fp = nullptr;
                fopen_s(&fp, pPath.c_str(), "rb");
                if (fp) {
                    char readBuffer[65536];
                    rapidjson::FileReadStream is(fp, readBuffer, sizeof(readBuffer));
                    rapidjson::Document doc;
                    doc.ParseStream(is);
                    fclose(fp);
                    ParseJSONToUI(doc);
                }
                SaveData();
                ApplyNPC();
            }
        }
        ImGuiMCP::Separator();
    }

    // --- PAINEL DE ATRIBUTOS BASE ---
    ImGuiMCP::Text("%s", GetLoc("text.base_attr", "Base Attributes & Offsets"));
    if (isLocked) ImGuiMCP::TextColored({ 0.5f, 0.5f, 0.5f, 1.0f }, "%s", GetLoc("text.locked", "[LOCKED]"));
    else {
        ImGuiMCP::SetNextItemWidth(200.0f);
        ImGuiMCP::InputFloat(GetLoc("ui.health", "Health"), &ui_health, 1.0f, 10.0f, "%.0f");
        ImGuiMCP::SameLine();
        ImGuiMCP::SetNextItemWidth(200.0f);
        ImGuiMCP::InputInt(GetLoc("ui.health_offset", "Health Offset"), &ui_healthOffset, 1, 10);

        ImGuiMCP::SetNextItemWidth(200.0f);
        ImGuiMCP::InputFloat(GetLoc("ui.magicka", "Magicka"), &ui_magicka, 1.0f, 10.0f, "%.0f");
        ImGuiMCP::SameLine();
        ImGuiMCP::SetNextItemWidth(200.0f);
        ImGuiMCP::InputInt(GetLoc("ui.magicka_offset", "Magicka Offset"), &ui_magickaOffset, 1, 10);

        ImGuiMCP::SetNextItemWidth(200.0f);
        ImGuiMCP::InputFloat(GetLoc("ui.stamina", "Stamina"), &ui_stamina, 1.0f, 10.0f, "%.0f");
        ImGuiMCP::SameLine();
        ImGuiMCP::SetNextItemWidth(200.0f);
        ImGuiMCP::InputInt(GetLoc("ui.stamina_offset", "Stamina Offset"), &ui_staminaOffset, 1, 10);

        ImGuiMCP::Spacing();
        ImGuiMCP::Text("%s", GetLoc("text.acbs", "Level & Configuration (ACBS)"));

        ImGuiMCP::Checkbox(GetLoc("ui.auto_calc", "Auto-Calc Stats"), &ui_calcStats);
        ImGuiMCP::SameLine();
        if (ui_calcStats) {
            ImGuiMCP::SetNextItemWidth(200.0f);
            ImGuiMCP::InputFloat(GetLoc("ui.level_mult", "Level Mult"), &ui_levelMult, 0.05f, 0.1f, "%.3f");
        }
        else {
            ImGuiMCP::SetNextItemWidth(200.0f);
            int lvl = ui_level;
            if (ImGuiMCP::InputInt(GetLoc("ui.level", "Level"), &lvl)) ui_level = static_cast<uint16_t>(std::max(1, lvl));
        }

        ImGuiMCP::SetNextItemWidth(200.0f);
        int cMin = ui_calcMinLevel;
        if (ImGuiMCP::InputInt(GetLoc("ui.calc_min", "Calc Min Level"), &cMin)) ui_calcMinLevel = static_cast<uint16_t>(std::max(0, cMin));
        ImGuiMCP::SameLine();
        ImGuiMCP::SetNextItemWidth(200.0f);
        int cMax = ui_calcMaxLevel;
        if (ImGuiMCP::InputInt(GetLoc("ui.calc_max", "Calc Max Level"), &cMax)) ui_calcMaxLevel = static_cast<uint16_t>(std::max(0, cMax));

        ImGuiMCP::SetNextItemWidth(200.0f);
        int spd = ui_speedMult;
        if (ImGuiMCP::InputInt(GetLoc("ui.speed_mult", "Speed Multiplier"), &spd)) ui_speedMult = static_cast<uint16_t>(std::max(0, spd));

        ImGuiMCP::SetNextItemWidth(200.0f);
        int disp = ui_dispositionBase;
        if (ImGuiMCP::InputInt(GetLoc("ui.disposition", "Disposition Base"), &disp)) ui_dispositionBase = static_cast<uint16_t>(std::max(0, disp));

        ImGuiMCP::SetNextItemWidth(200.0f);
        ImGuiMCP::InputInt(GetLoc("ui.bleedout", "Bleedout Override"), &ui_bleedoutOverride, 1);

        ImGuiMCP::Spacing();
        ImGuiMCP::Text("%s", GetLoc("text.base_flags", "Base Flags"));
        ImGuiMCP::Checkbox(GetLoc("ui.essential", "Essential"), &ui_isEssential);
        ImGuiMCP::SameLine();
        ImGuiMCP::Checkbox(GetLoc("ui.protected", "Protected"), &ui_isProtected);
        ImGuiMCP::SameLine();
        ImGuiMCP::Checkbox(GetLoc("ui.unique", "Unique"), &ui_isUnique);
        ImGuiMCP::SameLine();
        ImGuiMCP::Checkbox(GetLoc("ui.stealth_meter", "Doesn't affect stealth meter"), &ui_doesntAffectStealthMeter);
    }
    ImGuiMCP::Separator();
    ImGuiMCP::Spacing();
    ImGuiMCP::Text("%s", GetLoc("text.multipliers", "Actor Multipliers (Instance Values)"));
    if (isLocked) {
        ImGuiMCP::TextColored({ 0.5f, 0.5f, 0.5f, 1.0f }, "%s", GetLoc("text.locked", "[LOCKED]"));
    }
    else {
        ImGuiMCP::SetNextItemWidth(200.0f);
        ImGuiMCP::InputFloat(GetLoc("ui.atk_dmg_mult", "Attack Damage Mult"), &ui_attackDamageMult, 0.1f, 1.0f, "%.2f");
        ImGuiMCP::SameLine();
        ImGuiMCP::SetNextItemWidth(200.0f);
        ImGuiMCP::InputFloat(GetLoc("ui.heal_rate_mult", "Heal Rate Mult"), &ui_healRateMult, 1.0f, 10.0f, "%.2f");

        ImGuiMCP::SetNextItemWidth(200.0f);
        ImGuiMCP::InputFloat(GetLoc("ui.mag_rate_mult", "Magicka Rate Mult"), &ui_magickaRateMult, 1.0f, 10.0f, "%.2f");
        ImGuiMCP::SameLine();
        ImGuiMCP::SetNextItemWidth(200.0f);
        ImGuiMCP::InputFloat(GetLoc("ui.stam_rate_mult", "Stamina Rate Mult"), &ui_staminaRateMult, 1.0f, 10.0f, "%.2f");
    }
    ImGuiMCP::Separator();

    // --- PAINEL DE CLASSE E COMBATE ---
    ImGuiMCP::Text("%s", GetLoc("text.class_combat", "Class & Combat Style"));
    static int s_classIdx = 0, s_combatIdx = 0;
    DrawDropdown(GetLoc("ui.class", "Class"), "Class", &ui_class, s_classIdx, isLocked, 300.0f);
    DrawDropdown(GetLoc("ui.combat_style", "Combat Style"), "CombatStyle", &ui_combatStyle, s_combatIdx, isLocked, 300.0f);
    ImGuiMCP::Separator();

    // --- PAINEL DE SKILLS E OFFSETS ---
    ImGuiMCP::Text("%s", GetLoc("text.skills", "Skills (Base & Offsets)"));
    if (ImGuiMCP::BeginTable("SkillsTable", 3, ImGuiMCP::ImGuiTableFlags_Borders | ImGuiMCP::ImGuiTableFlags_RowBg)) {
        ImGuiMCP::TableSetupColumn(GetLoc("col.skill", "Skill"), ImGuiMCP::ImGuiTableColumnFlags_WidthFixed, 200.0f);
        ImGuiMCP::TableSetupColumn(GetLoc("col.value", "Value"), ImGuiMCP::ImGuiTableColumnFlags_WidthFixed, 200.0f);
        ImGuiMCP::TableSetupColumn(GetLoc("col.offset", "Offset"), ImGuiMCP::ImGuiTableColumnFlags_WidthFixed, 200.0f);
        ImGuiMCP::TableHeadersRow();

        for (int i = 0; i < 18; i++) {
            ImGuiMCP::TableNextRow();
            ImGuiMCP::TableSetColumnIndex(0);
            ImGuiMCP::Text("%s", skillNames[i]);

            ImGuiMCP::TableSetColumnIndex(1);
            ImGuiMCP::PushID(i);
            if (isLocked) ImGuiMCP::Text("%d", ui_skills[i]);
            else {
                int skillVal = ui_skills[i];
                ImGuiMCP::SetNextItemWidth(200.0f);
                if (ImGuiMCP::InputInt("##val", &skillVal, 1, 5)) {
                    if (skillVal < 0) skillVal = 0;
                    if (skillVal > 255) skillVal = 255;
                    ui_skills[i] = static_cast<uint8_t>(skillVal);
                }
            }
            ImGuiMCP::PopID();

            ImGuiMCP::TableSetColumnIndex(2);
            ImGuiMCP::PushID(i + 100);
            if (isLocked) ImGuiMCP::Text("%d", ui_skillOffsets[i]);
            else {
                int offsetVal = ui_skillOffsets[i];
                ImGuiMCP::SetNextItemWidth(200.0f);
                if (ImGuiMCP::InputInt("##off", &offsetVal, 1, 5)) {
                    if (offsetVal < 0) offsetVal = 0;
                    if (offsetVal > 255) offsetVal = 255;
                    ui_skillOffsets[i] = static_cast<uint8_t>(offsetVal);
                }
            }
            ImGuiMCP::PopID();
        }
        ImGuiMCP::EndTable();
    }
    ImGuiMCP::Separator();

    // --- PAINEL DE PERKS ---
    ImGuiMCP::Text("%s", GetLoc("text.perks", "Perks"));
    if (ImGuiMCP::BeginTable("PerksTable", 3, ImGuiMCP::ImGuiTableFlags_Borders | ImGuiMCP::ImGuiTableFlags_RowBg | ImGuiMCP::ImGuiTableFlags_Resizable)) {
        ImGuiMCP::TableSetupColumn(GetLoc("col.action", "Action"), ImGuiMCP::ImGuiTableColumnFlags_WidthFixed, 80.0f);
        ImGuiMCP::TableSetupColumn(GetLoc("col.perk_edid", "Perk EditorID"), ImGuiMCP::ImGuiTableColumnFlags_WidthStretch);
        ImGuiMCP::TableSetupColumn(GetLoc("col.rank", "Rank"), ImGuiMCP::ImGuiTableColumnFlags_WidthFixed, 200.0f);
        ImGuiMCP::TableHeadersRow();

        int to_remove_perk = -1;
        for (size_t i = 0; i < ui_perks.size(); i++) {
            ImGuiMCP::PushID(static_cast<int>(i));
            ImGuiMCP::TableNextRow();

            ImGuiMCP::TableSetColumnIndex(0);
            if (!isLocked && ImGuiMCP::Button(" X ")) to_remove_perk = static_cast<int>(i);

            ImGuiMCP::TableSetColumnIndex(1);
            ImGuiMCP::Text("%s", ui_perks[i].perk ? clib_util::editorID::get_editorID(ui_perks[i].perk).c_str() : "Null");

            ImGuiMCP::TableSetColumnIndex(2);
            if (isLocked) {
                ImGuiMCP::Text("%d", ui_perks[i].rank);
            }
            else {
                ImGuiMCP::SetNextItemWidth(-1.0f);
                ImGuiMCP::InputInt("##rank", &ui_perks[i].rank, 1);
            }
            ImGuiMCP::PopID();
        }
        ImGuiMCP::EndTable();
        if (to_remove_perk != -1) ui_perks.erase(ui_perks.begin() + to_remove_perk);
    }
    if (!isLocked) {
        static RE::BGSPerk* newPerk = nullptr;
        static int s_newPerkIdx = 0;
        DrawDropdown(GetLoc("ui.add_perk", "Add Perk"), "Perk", &newPerk, s_newPerkIdx, false, 300.0f);
        ImGuiMCP::SameLine();
        std::string addBtnStr = std::string(GetLoc("btn.add", "Add")) + "##Perk";
        if (ImGuiMCP::Button(addBtnStr.c_str()) && newPerk) {
            bool exists = false;
            for (const auto& p : ui_perks) { if (p.perk == newPerk) exists = true; }
            if (!exists) ui_perks.push_back({ newPerk, 1 });
        }
    }
    ImGuiMCP::Separator();

    // --- PAINEL DE FACTIONS ---
    ImGuiMCP::Text("%s", GetLoc("text.factions", "Factions"));
    if (ImGuiMCP::BeginTable("FactionsTable", 3, ImGuiMCP::ImGuiTableFlags_Borders | ImGuiMCP::ImGuiTableFlags_RowBg | ImGuiMCP::ImGuiTableFlags_Resizable)) {
        ImGuiMCP::TableSetupColumn(GetLoc("col.action", "Action"), ImGuiMCP::ImGuiTableColumnFlags_WidthFixed, 80.0f);
        ImGuiMCP::TableSetupColumn(GetLoc("col.faction_edid", "Faction EditorID"), ImGuiMCP::ImGuiTableColumnFlags_WidthStretch);
        ImGuiMCP::TableSetupColumn(GetLoc("col.rank", "Rank"), ImGuiMCP::ImGuiTableColumnFlags_WidthFixed, 200.0f);
        ImGuiMCP::TableHeadersRow();

        int to_remove_faction = -1;
        for (size_t i = 0; i < ui_factions.size(); i++) {
            ImGuiMCP::PushID(static_cast<int>(i + 1000));
            ImGuiMCP::TableNextRow();

            ImGuiMCP::TableSetColumnIndex(0);
            if (!isLocked && ImGuiMCP::Button(" X ")) to_remove_faction = static_cast<int>(i);

            ImGuiMCP::TableSetColumnIndex(1);
            ImGuiMCP::Text("%s", ui_factions[i].faction ? clib_util::editorID::get_editorID(ui_factions[i].faction).c_str() : "Null");

            ImGuiMCP::TableSetColumnIndex(2);
            if (isLocked) {
                ImGuiMCP::Text("%d", ui_factions[i].rank);
            }
            else {
                ImGuiMCP::SetNextItemWidth(-1.0f);
                ImGuiMCP::InputInt("##rank", &ui_factions[i].rank, 1);
            }
            ImGuiMCP::PopID();
        }
        ImGuiMCP::EndTable();
        if (to_remove_faction != -1) ui_factions.erase(ui_factions.begin() + to_remove_faction);
    }
    if (!isLocked) {
        static RE::TESFaction* newFaction = nullptr;
        static int s_newFactionIdx = 0;
        DrawDropdown(GetLoc("ui.add_faction", "Add Faction"), "Faction", &newFaction, s_newFactionIdx, false, 300.0f);
        ImGuiMCP::SameLine();
        std::string addFacBtnStr = std::string(GetLoc("btn.add", "Add")) + "##Faction";
        if (ImGuiMCP::Button(addFacBtnStr.c_str()) && newFaction) {
            bool exists = false;
            for (const auto& f : ui_factions) { if (f.faction == newFaction) exists = true; }
            if (!exists) ui_factions.push_back({ newFaction, 0 });
        }
    }

    ImGuiMCP::Text("%s", GetLoc("text.spells", "Spells"));
    if (ImGuiMCP::BeginTable("SpellsTable", 2, ImGuiMCP::ImGuiTableFlags_Borders | ImGuiMCP::ImGuiTableFlags_RowBg | ImGuiMCP::ImGuiTableFlags_Resizable)) {
        ImGuiMCP::TableSetupColumn(GetLoc("col.action", "Action"), ImGuiMCP::ImGuiTableColumnFlags_WidthFixed, 80.0f);
        ImGuiMCP::TableSetupColumn(GetLoc("col.spell_edid", "Spell EditorID"), ImGuiMCP::ImGuiTableColumnFlags_WidthStretch);
        ImGuiMCP::TableHeadersRow();

        int to_remove_spell = -1;
        for (size_t i = 0; i < ui_spells.size(); i++) {
            ImGuiMCP::PushID(static_cast<int>(i + 2000));
            ImGuiMCP::TableNextRow();

            ImGuiMCP::TableSetColumnIndex(0);
            if (!isLocked && ImGuiMCP::Button(" X ")) to_remove_spell = static_cast<int>(i);

            ImGuiMCP::TableSetColumnIndex(1);
            ImGuiMCP::Text("%s", ui_spells[i] ? clib_util::editorID::get_editorID(ui_spells[i]).c_str() : "Null");

            ImGuiMCP::PopID();
        }
        ImGuiMCP::EndTable();
        if (to_remove_spell != -1) ui_spells.erase(ui_spells.begin() + to_remove_spell);
    }
    if (!isLocked) {
        static RE::SpellItem* newSpell = nullptr;
        static int s_newSpellIdx = 0;
        DrawDropdown(GetLoc("ui.add_spell", "Add Spell"), "Spell", &newSpell, s_newSpellIdx, false, 300.0f);
        ImGuiMCP::SameLine();
        std::string addSpellBtnStr = std::string(GetLoc("btn.add", "Add")) + "##Spell";
        if (ImGuiMCP::Button(addSpellBtnStr.c_str()) && newSpell) {
            if (std::find(ui_spells.begin(), ui_spells.end(), newSpell) == ui_spells.end()) {
                ui_spells.push_back(newSpell);
            }
        }
    }
    ImGuiMCP::Separator();
}

void NSettings::Presets() {
    ImGuiMCP::Text("%s", GetLoc("text.preset_manager", "Stats Preset Manager"));
    ImGuiMCP::Separator();
    RefreshAvailablePresets();

    static std::map<std::string, std::vector<std::string>> presetUsageDB;
    static bool needsUsageScan = true;
    if (ImGuiMCP::Button(GetLoc("btn.refresh_usage", "Refresh Usage List")) || needsUsageScan) {
        presetUsageDB.clear();
        if (std::filesystem::exists(NPCPath)) {
            for (const auto& entry : std::filesystem::directory_iterator(NPCPath)) {
                if (entry.path().extension() == ".json") {
                    FILE* fp = nullptr;
                    fopen_s(&fp, entry.path().string().c_str(), "rb");
                    if (fp) {
                        char readBuffer[2048];
                        rapidjson::FileReadStream is(fp, readBuffer, sizeof(readBuffer));
                        rapidjson::Document doc;
                        doc.ParseStream(is);
                        fclose(fp);
                        if (doc.IsObject() && doc.HasMember("preset") && doc["preset"].IsString()) {
                            presetUsageDB[doc["preset"].GetString()].push_back(entry.path().stem().string());
                        }
                    }
                }
            }
        }
        needsUsageScan = false;
    }

    auto tableFlags = ImGuiMCP::ImGuiTableFlags_Borders | ImGuiMCP::ImGuiTableFlags_RowBg | ImGuiMCP::ImGuiTableFlags_Resizable;
    static bool openDeleteModal = false;
    static std::string presetToDelete = "";
    static bool deleteLinkedNPCs = false;

    if (ImGuiMCP::BeginTable("PresetDB", 5, tableFlags)) {
        ImGuiMCP::TableSetupColumn(GetLoc("col.edit", "Edit"), ImGuiMCP::ImGuiTableColumnFlags_WidthFixed, 60.0f);
        ImGuiMCP::TableSetupColumn(GetLoc("col.export", "Export"), ImGuiMCP::ImGuiTableColumnFlags_WidthFixed, 80.0f);
        ImGuiMCP::TableSetupColumn(GetLoc("col.delete", "Delete"), ImGuiMCP::ImGuiTableColumnFlags_WidthFixed, 80.0f);
        ImGuiMCP::TableSetupColumn(GetLoc("col.preset_name", "Preset Name"), ImGuiMCP::ImGuiTableColumnFlags_WidthStretch);
        ImGuiMCP::TableSetupColumn(GetLoc("col.affected_npcs", "Affected NPCs"), ImGuiMCP::ImGuiTableColumnFlags_WidthStretch);
        ImGuiMCP::TableHeadersRow();

        for (const auto& pName : ui_availablePresets) {
            ImGuiMCP::PushID(pName.c_str());
            ImGuiMCP::TableNextRow();

            ImGuiMCP::TableSetColumnIndex(0);
            if (ImGuiMCP::Button(GetLoc("col.edit", "Edit"))) LoadPresetToUI(pName);

            ImGuiMCP::TableSetColumnIndex(1);
            if (isEditingPreset && activePresetName == pName && HasUnsavedChanges()) {
                ImGuiMCP::TextColored({ 0.5f, 0.5f, 0.5f, 1.0f }, "%s", GetLoc("text.save_first", "Save First"));
            }
            else {
                if (ImGuiMCP::Button(GetLoc("col.export", "Export"))) ExportPresetAsZip(pName);
            }

            ImGuiMCP::TableSetColumnIndex(2);
            ImGuiMCP::PushStyleColor(ImGuiMCP::ImGuiCol_Button, { 0.8f, 0.2f, 0.2f, 1.0f });
            if (ImGuiMCP::Button(GetLoc("col.delete", "Delete"))) {
                presetToDelete = pName;
                deleteLinkedNPCs = false;
                openDeleteModal = true;
            }
            ImGuiMCP::PopStyleColor();

            ImGuiMCP::TableSetColumnIndex(3);
            ImGuiMCP::Text("%s", pName.c_str());

            ImGuiMCP::TableSetColumnIndex(4);
            std::string users = "";
            for (const auto& u : presetUsageDB[pName]) users += u + ", ";
            if (!users.empty()) { users.pop_back(); users.pop_back(); }
            ImGuiMCP::TextWrapped("%s", users.empty() ? GetLoc("text.none", "None") : users.c_str());

            ImGuiMCP::PopID();
        }
        ImGuiMCP::EndTable();
    }

    // Modal de Delete
    const char* popupDeleteLabel = GetLoc("modal.confirm_delete", "Confirm Delete Preset");
    if (openDeleteModal) ImGuiMCP::OpenPopup(popupDeleteLabel);

    if (ImGuiMCP::BeginPopupModal(popupDeleteLabel, &openDeleteModal, ImGuiMCP::ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGuiMCP::Text(GetLoc("ui.delete_preset_q", "Delete preset '%s'?"), presetToDelete.c_str());
        ImGuiMCP::Separator();
        auto& users = presetUsageDB[presetToDelete];
        if (!users.empty()) {
            ImGuiMCP::TextColored({ 1.0f, 0.6f, 0.2f, 1.0f }, GetLoc("text.warning_used", "Warning: Used by %d NPC(s)."), (int)users.size());
            ImGuiMCP::Checkbox(GetLoc("text.delete_linked", "Also delete the JSON edits for these affected NPCs?"), &deleteLinkedNPCs);
        }
        if (ImGuiMCP::Button(GetLoc("btn.yes_delete", "Yes, Delete"), ImGuiMCP::ImVec2(120, 0))) {
            std::string presetPath = std::format("{}/{}.json", PresetsPath, presetToDelete);
            if (std::filesystem::exists(presetPath)) std::filesystem::remove(presetPath);
            if (deleteLinkedNPCs && !users.empty()) {
                for (const auto& u : users) {
                    std::string nPath = std::format("{}/{}.json", NPCPath, u);
                    if (std::filesystem::exists(nPath)) std::filesystem::remove(nPath);
                }
            }
            if (activePresetName == presetToDelete) { activePresetName = ""; isEditingPreset = false; }
            if (ui_linkedPreset == presetToDelete) ui_linkedPreset = "";
            needsUsageScan = true;
            openDeleteModal = false;
            ImGuiMCP::CloseCurrentPopup();
        }
        ImGuiMCP::SameLine();
        if (ImGuiMCP::Button(GetLoc("btn.cancel", "Cancel"), ImGuiMCP::ImVec2(120, 0))) { openDeleteModal = false; ImGuiMCP::CloseCurrentPopup(); }
        ImGuiMCP::EndPopup();
    }
}

void NSettings::NPCList() {
    auto manager = Manager::GetSingleton();
    const auto& npcList = manager->GetList("NPC");

    if (npcList.empty()) {
        ImGuiMCP::Text("%s", GetLoc("text.no_npcs", "No NPCs loaded into memory."));
        if (ImGuiMCP::Button(GetLoc("btn.force_scan", "Force Scan"))) manager->PopulateAllLists();
        return;
    }

    static std::map<std::string, std::string> affectedDB;
    static bool needScan = true;

    if (needScan) {
        affectedDB.clear();
        if (std::filesystem::exists(NPCPath)) {
            for (const auto& entry : std::filesystem::directory_iterator(NPCPath)) {
                if (entry.path().extension() == ".json") {
                    std::string stem = entry.path().stem().string();
                    std::string presetLinked = "Custom";
                    FILE* fp = nullptr;
                    fopen_s(&fp, entry.path().string().c_str(), "rb");
                    if (fp) {
                        char readBuffer[2048];
                        rapidjson::FileReadStream is(fp, readBuffer, sizeof(readBuffer));
                        rapidjson::Document doc;
                        doc.ParseStream(is);
                        fclose(fp);
                        if (doc.IsObject() && doc.HasMember("preset") && doc["preset"].IsString()) {
                            presetLinked = doc["preset"].GetString();
                        }
                    }
                    affectedDB[stem] = presetLinked;
                }
            }
        }
        needScan = false;
    }

    if (ImGuiMCP::Button(GetLoc("btn.refresh_db", "Refresh Database"))) needScan = true;

    static char filterBuffer[256] = "";
    static bool showOnlyAffected = false;
    static std::vector<size_t> cachedFilteredIndices;

    ImGuiMCP::SetNextItemWidth(200.0f);
    bool searchChanged = ImGuiMCP::InputText(GetLoc("ui.filter_name", "Filter Name/EditorID"), filterBuffer, sizeof(filterBuffer));
    ImGuiMCP::SameLine();
    bool toggleChanged = ImGuiMCP::Checkbox(GetLoc("ui.only_modified", "Only Modified NPCs"), &showOnlyAffected);

    if (searchChanged || toggleChanged || cachedFilteredIndices.empty()) {
        cachedFilteredIndices.clear();
        std::string search(filterBuffer);
        std::transform(search.begin(), search.end(), search.begin(), [](unsigned char c) { return std::tolower(c); });

        for (size_t i = 0; i < npcList.size(); i++) {
            const auto& item = npcList[i];
            std::string edidHex = std::format("{:08X}", item.formID);
            bool isAffected = affectedDB.contains(item.editorID) || affectedDB.contains(edidHex);
            if (showOnlyAffected && !isAffected) continue;

            if (!search.empty()) {
                std::string n = item.name; std::transform(n.begin(), n.end(), n.begin(), [](unsigned char c) { return std::tolower(c); });
                std::string e = item.editorID; std::transform(e.begin(), e.end(), e.begin(), [](unsigned char c) { return std::tolower(c); });
                if (n.find(search) == std::string::npos && e.find(search) == std::string::npos) continue;
            }
            cachedFilteredIndices.push_back(i);
        }
    }

    ImGuiMCP::Text(GetLoc("text.showing_npcs", "Showing %d NPCs"), (int)cachedFilteredIndices.size());
    ImGuiMCP::Separator();

    auto tableFlags = ImGuiMCP::ImGuiTableFlags_Borders | ImGuiMCP::ImGuiTableFlags_RowBg |
        ImGuiMCP::ImGuiTableFlags_Resizable | ImGuiMCP::ImGuiTableFlags_ScrollY;

    if (ImGuiMCP::BeginTable("NPCDatabaseTable", 4, tableFlags)) {
        ImGuiMCP::TableSetupColumn(GetLoc("col.action", "Action"), ImGuiMCP::ImGuiTableColumnFlags_WidthFixed, 80.0f);
        ImGuiMCP::TableSetupColumn(GetLoc("col.formid", "FormID"), ImGuiMCP::ImGuiTableColumnFlags_WidthFixed, 90.0f);
        ImGuiMCP::TableSetupColumn(GetLoc("col.name_edid", "Name / EditorID"), ImGuiMCP::ImGuiTableColumnFlags_WidthStretch);
        ImGuiMCP::TableSetupColumn(GetLoc("col.status", "Status"), ImGuiMCP::ImGuiTableColumnFlags_WidthFixed, 150.0f);
        ImGuiMCP::TableHeadersRow();

        static auto clipper = ImGuiMCP::ImGuiListClipperManager::Create();
        ImGuiMCP::ImGuiListClipperManager::Begin(clipper, (int)cachedFilteredIndices.size(), -1.0f);

        while (ImGuiMCP::ImGuiListClipperManager::Step(clipper)) {
            for (int i = clipper->DisplayStart; i < clipper->DisplayEnd; i++) {
                size_t idx = cachedFilteredIndices[i];
                const auto& item = npcList[idx];
                std::string edidHex = std::format("{:08X}", item.formID);
                auto dbIt = affectedDB.find(item.editorID);
                if (dbIt == affectedDB.end()) dbIt = affectedDB.find(edidHex);

                ImGuiMCP::TableNextRow();
                ImGuiMCP::TableSetColumnIndex(0);
                ImGuiMCP::PushID(static_cast<int>(item.formID));
                if (ImGuiMCP::Button(GetLoc("col.edit", "Edit"))) {
                    if (auto npc = RE::TESForm::LookupByID<RE::TESNPC>(item.formID)) {
                        LoadNPCToUI(npc, nullptr);
                    }
                }
                ImGuiMCP::PopID();

                ImGuiMCP::TableSetColumnIndex(1); ImGuiMCP::Text("%08X", item.formID);
                ImGuiMCP::TableSetColumnIndex(2); ImGuiMCP::TextUnformatted(item.name.empty() ? item.editorID.c_str() : item.name.c_str());

                ImGuiMCP::TableSetColumnIndex(3);
                if (dbIt != affectedDB.end()) {
                    if (dbIt->second == "Custom") ImGuiMCP::TextColored({ 0.2f, 1.0f, 0.2f, 1.0f }, "%s", GetLoc("text.modified", "MODIFIED"));
                    else ImGuiMCP::TextColored({ 0.2f, 1.0f, 1.0f, 1.0f }, GetLoc("text.preset_status", "Preset: %s"), dbIt->second.c_str());
                }
                else {
                    ImGuiMCP::TextDisabled("%s", GetLoc("text.default", "Default"));
                }
            }
        }
        ImGuiMCP::ImGuiListClipperManager::End(clipper);
        ImGuiMCP::EndTable();
    }
}

void NSettings::NPCMenu() {
    if (!isEditingPreset) {
        if (ImGuiMCP::Button(GetLoc("btn.load_npc", "Load Selected NPC (Console)"))) {
            LoadNPCToUI();
        }
    }

    if (isEditingPreset) {
        ImGuiMCP::TextColored({ 0.2f, 1.0f, 0.2f, 1.0f }, GetLoc("text.preset_edit_mode", "PRESET EDIT MODE: %s"), activePresetName.c_str());
        if (ImGuiMCP::Button(GetLoc("btn.exit_preset", "<- Exit Preset Editor"))) {
            isEditingPreset = false;
            activePresetName = "";
            g_currentNPC = nullptr;
        }
        ImGuiMCP::Separator();
        DrawMainEditorUI();
    }
    else {
        if (!g_currentNPC) {
            ImGuiMCP::Text("%s", GetLoc("text.open_console", "Open the game console, select an NPC and press 'Load'."));
        }
        else {
            ImGuiMCP::Text(GetLoc("text.editing_npc", "Editing NPC: %s [%08X]"),
                g_currentNPC->GetFullName() ? g_currentNPC->GetFullName() : "Unnamed",
                g_currentNPC->GetFormID());
            ImGuiMCP::Separator();
            DrawMainEditorUI();
        }
    }
}
void NSettings::Load() {
    logger::info("[Load] Inicializando sistema de arquivos de Stats...");
    std::filesystem::create_directories(NPCPath);
    std::filesystem::create_directories(PresetsPath);

    int countPresetsCarregados = 0;
    int countNPCsModificados = 0;
    std::map<std::string, rapidjson::Document> presetCache;

    for (const auto& entry : std::filesystem::directory_iterator(PresetsPath)) {
        if (entry.path().extension() == ".json") {
            std::string presetName = entry.path().stem().string();
            FILE* fp = nullptr;
            fopen_s(&fp, entry.path().string().c_str(), "rb");
            if (fp) {
                try {
                    char readBuffer[65536];
                    rapidjson::FileReadStream is(fp, readBuffer, sizeof(readBuffer));
                    rapidjson::Document doc;
                    doc.ParseStream(is);
                    fclose(fp);
                    if (doc.IsObject()) {
                        presetCache[presetName] = std::move(doc);
                        countPresetsCarregados++;
                    }
                }
                catch (...) { if (fp) fclose(fp); }
            }
        }
    }

    for (const auto& entry : std::filesystem::directory_iterator(NPCPath)) {
        if (entry.path().extension() == ".json") {
            std::string filename = entry.path().stem().string();
            RE::TESNPC* targetNPC = nullptr;

            if (auto edidForm = RE::TESForm::LookupByEditorID(filename)) targetNPC = edidForm->As<RE::TESNPC>();
            else {
                try { RE::FormID id = std::stoul(filename, nullptr, 16); if (auto f = RE::TESForm::LookupByID(id)) targetNPC = f->As<RE::TESNPC>(); }
                catch (...) {}
            }
            if (!targetNPC) continue;

            if (!g_vanillaNPCStates.contains(targetNPC->GetFormID())) {
                std::string vanillaStr;
                CaptureVanillaState(targetNPC, vanillaStr);
                g_vanillaNPCStates[targetNPC->GetFormID()] = vanillaStr;
            }

            FILE* fp = nullptr;
            fopen_s(&fp, entry.path().string().c_str(), "rb");
            if (fp) {
                try {
                    char readBuffer[65536];
                    rapidjson::FileReadStream is(fp, readBuffer, sizeof(readBuffer));
                    rapidjson::Document doc;
                    doc.ParseStream(is);
                    fclose(fp);

                    if (doc.IsObject()) {
                        if (doc.HasMember("preset") && doc["preset"].IsString()) {
                            std::string presetName = doc["preset"].GetString();
                            if (presetCache.find(presetName) != presetCache.end()) {
                                Manager::ApplyNPCCustomizationFromJSON(targetNPC, presetCache[presetName]);
                                countNPCsModificados++;
                            }
                        }
                        else {
                            Manager::ApplyNPCCustomizationFromJSON(targetNPC, doc);
                            countNPCsModificados++;
                        }
                    }
                }
                catch (...) { if (fp) fclose(fp); }
            }
        }
    }
    logger::info("[NPC Stats Replacer] BOOT CONCLUIDO: {} presets em cache, {} NPCs modificados.", countPresetsCarregados, countNPCsModificados);
}

void NSettings::MmRegister() {
    if (SKSEMenuFramework::IsInstalled()) {
        // Carrega o idioma no momento do Registro do menu!
        LoadLanguage();

        SKSEMenuFramework::SetSection("NPC Stats Editor");
        SKSEMenuFramework::AddSectionItem(GetLoc("menu.editor", "Editor"), NPCMenu);
        SKSEMenuFramework::AddSectionItem(GetLoc("menu.presets", "Presets"), Presets);
        SKSEMenuFramework::AddSectionItem(GetLoc("menu.database", "Database"), NPCList);
        logger::info("[MmRegister] Stats Menu sections registered successfully.");
    }
}