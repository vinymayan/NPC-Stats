#include "Manager.h"

namespace FormUtil {
    const RE::TESFile* GetMasterFile(RE::TESForm* ref) {
        if (!ref) return nullptr;

        uint32_t formID = ref->GetFormID();
        uint8_t modIndex = static_cast<uint8_t>(formID >> 24);

        auto dataHandler = RE::TESDataHandler::GetSingleton();
        if (!dataHandler) return nullptr;

        if (modIndex == 0xFE) {
            uint16_t eslIndex = (formID >> 12) & 0xFFF;
            return dataHandler->LookupLoadedLightModByIndex(eslIndex);
        }

        return dataHandler->LookupLoadedModByIndex(modIndex);
    }

    std::string NormalizeFormID(RE::TESForm* form) {
        if (!form) return {};

        RE::FormID formID = form->GetFormID();
        uint8_t modIndex = (formID >> 24) & 0xFF;

        if (modIndex == 0xFF) {
            return std::format("{:X}", formID);
        }

        auto file = GetMasterFile(form);
        if (!file) return std::format("{:X}", formID);

        uint32_t localID = formID & 0x00FFFFFF;

        if (modIndex == 0xFE) {
            uint32_t eslID = localID & 0xFFF;
            return std::format("{}|{:X}", file->GetFilename(), eslID);
        }

        return std::format("{}|{:X}", file->GetFilename(), localID);
    }

    // Função auxiliar para reverter string para FormID no load do JSON
    RE::FormID FormIDFromString(const std::string& str) {
        auto pos = str.find('|');
        if (pos != std::string::npos) {
            std::string plugin = str.substr(0, pos);
            std::string idStr = str.substr(pos + 1);
            RE::FormID localId = std::stoul(idStr, nullptr, 16);
            auto dataHandler = RE::TESDataHandler::GetSingleton();
            return dataHandler ? dataHandler->LookupFormID(localId, plugin) : 0;
        }
        return str.empty() ? 0 : std::stoul(str, nullptr, 16);
    }
}

void Manager::PopulateAllLists() {
    if (_isPopulated) return;

    logger::info("Iniciando escaneamento de FormTypes...");

    PopulateList<RE::TESNPC>("NPC");
    PopulateList<RE::TESClass>("Class");
    PopulateList<RE::TESCombatStyle>("CombatStyle");
    PopulateList<RE::BGSPerk>("Perk");
    PopulateList<RE::TESFaction>("Faction");
    PopulateList<RE::SpellItem>("Spell");
    _isPopulated = true;
    for (auto cb : _readyCallbacks) {
        if (cb) cb();
    }
    _readyCallbacks.clear();
}

void Manager::RefreshLists(std::string_view a_signatures) {
    const auto includes = [a_signatures](std::string_view a_signature) {
        const auto equalsIgnoreCase = [](std::string_view a_left, std::string_view a_right) {
            if (a_left.size() != a_right.size()) return false;

            for (std::size_t i = 0; i < a_left.size(); ++i) {
                const auto toUpperASCII = [](char a_character) {
                    return a_character >= 'a' && a_character <= 'z' ?
                        static_cast<char>(a_character - ('a' - 'A')) :
                        a_character;
                };
                if (toUpperASCII(a_left[i]) != toUpperASCII(a_right[i])) return false;
            }
            return true;
        };

        std::size_t begin = 0;
        while (begin <= a_signatures.size()) {
            const auto end = a_signatures.find(',', begin);
            auto token = a_signatures.substr(begin, end == std::string_view::npos ? a_signatures.size() - begin : end - begin);
            while (!token.empty() && token.front() == ' ') token.remove_prefix(1);
            while (!token.empty() && token.back() == ' ') token.remove_suffix(1);
            if (equalsIgnoreCase(token, a_signature)) return true;
            if (end == std::string_view::npos) break;
            begin = end + 1;
        }
        return false;
    };

    if (a_signatures.empty() || includes("All")) {
        _isPopulated = false;
        PopulateAllLists();
        return;
    }
    if (includes("NPC_")) PopulateList<RE::TESNPC>("NPC");
    if (includes("CLAS")) PopulateList<RE::TESClass>("Class");
    if (includes("CSTY")) PopulateList<RE::TESCombatStyle>("CombatStyle");
    if (includes("PERK")) PopulateList<RE::BGSPerk>("Perk");
    if (includes("FACT")) PopulateList<RE::TESFaction>("Faction");
    if (includes("SPEL")) PopulateList<RE::SpellItem>("Spell");
}

const std::vector<InternalFormInfo>& Manager::GetList(const std::string& typeName) {
    static std::vector<InternalFormInfo> empty;
    auto it = _dataStore.find(typeName);
    if (it != _dataStore.end()) {
        return it->second;
    }
    return empty;
}

void Manager::RegisterReadyCallback(std::function<void()> callback) {
    if (_isPopulated) {
        callback();
    }
    else {
        _readyCallbacks.push_back(callback);
    }
}


std::string Manager::ToUTF8(std::string_view a_str) {
    if (a_str.empty()) return "";

    // 1. Testa se a string já é um UTF-8 válido
    int u8Test = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, a_str.data(), static_cast<int>(a_str.size()), nullptr, 0);
    if (u8Test > 0) {
        // É UTF-8 válido (Skyrim SE nativo), retorna sem corromper
        return std::string(a_str);
    }

    // 2. Se falhou, a string é ANSI (Mod antigo ou locale específico do Windows).
    // Precisamos converter de ANSI (CP_ACP) para UTF-16, e depois para UTF-8.
    int wlen = MultiByteToWideChar(CP_ACP, 0, a_str.data(), static_cast<int>(a_str.size()), nullptr, 0);
    if (wlen <= 0) return std::string(a_str);

    std::wstring wstr(wlen, 0);
    MultiByteToWideChar(CP_ACP, 0, a_str.data(), static_cast<int>(a_str.size()), &wstr[0], wlen);

    int u8len = WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(), wlen, nullptr, 0, nullptr, nullptr);
    if (u8len <= 0) return std::string(a_str);

    std::string u8str(u8len, 0);
    WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(), wlen, &u8str[0], u8len, nullptr, nullptr);

    return u8str;
}

template <typename T>
void Manager::PopulateList(const std::string& a_typeName, std::function<bool(T*)> a_filter) {
    auto dataHandler = RE::TESDataHandler::GetSingleton();
    if (!dataHandler) return;

    auto& list = _dataStore[a_typeName];
    list.clear();

    const auto& forms = dataHandler->GetFormArray<T>();
    list.reserve(forms.size());

    for (const auto& form : forms) {
        if (!form) continue;

        if (form->IsDeleted() || form->IsIgnored()) {
            continue;
        }

        if (a_filter && !a_filter(form)) {
            continue;
        }
        // Variáveis de auxílio para o log de erro caso o catch seja acionado
        RE::FormID currentID = 0;
        std::string currentPlugin = "Unknown";

        try {
            currentID = form->GetFormID();

            // Obtém o nome do plugin de origem antes de qualquer processamento complexo
            if (auto file = form->GetFile(0)) {
                currentPlugin = std::string(file->GetFilename());
            }
            else {
                currentPlugin = "Dynamic";
            }

            InternalFormInfo info;
            info.formID = currentID;
            info.formType = a_typeName;
            info.pluginName = ToUTF8(currentPlugin);

            // EditorID: clib_util pode lançar exceções em contextos raros de memória
            std::string rawEditorID = clib_util::editorID::get_editorID(form);
            info.editorID = ToUTF8(rawEditorID);

            std::string rawName = "";
            if (form->Is(RE::FormType::NPC)) {
                if (auto npc = form->As<RE::TESNPC>()) {
                    rawName = npc->fullName.c_str();
                }
            }
            else if (auto fullName = form->As<RE::TESFullName>()) {
                rawName = fullName->fullName.c_str();
            }

            // A conversão UTF-8 é um ponto comum de falha se a string estiver corrompida
            info.name = ToUTF8(rawName);
            info.UpdateDisplayName();
            list.push_back(info);
        }
        catch (const std::exception& e) {
            // Log detalhado com FormID em Hexadecimal e o erro específico
            logger::error("[PopulateList] Critical error on item {:08X} of plugin '{}' (Type: {}). Error: {}",
                currentID, currentPlugin, a_typeName, e.what());
        }
        catch (...) {
            // Captura erros desconhecidos que não herdam de std::exception
            logger::error("[PopulateList] Uknown error on item {:08X} of plugin '{}' (Type: {})",
                currentID, currentPlugin, a_typeName);
        }
    }
    logger::info("Carregados {} itens do tipo {}", list.size(), a_typeName);
}



Manager::NPCCollectionState& Manager::GetOrCaptureCollectionState(RE::TESNPC* a_npc) {
    auto& state = _npcCollectionStates[a_npc->GetFormID()];
    if (state.captured) return state;

    for (std::uint32_t i = 0; i < a_npc->perkCount; i++) {
        if (a_npc->perks && a_npc->perks[i].perk) {
            state.basePerks[a_npc->perks[i].perk] = a_npc->perks[i].currentRank;
        }
    }

    for (const auto& factionRank : a_npc->factions) {
        if (factionRank.faction) {
            state.baseFactions[factionRank.faction] = factionRank.rank;
        }
    }

    auto spellList = static_cast<RE::TESSpellList*>(a_npc);
    if (spellList->actorEffects) {
        for (std::uint32_t i = 0; i < spellList->actorEffects->numSpells; i++) {
            if (spellList->actorEffects->spells && spellList->actorEffects->spells[i]) {
                state.baseSpells.insert(spellList->actorEffects->spells[i]);
            }
        }
    }

    state.captured = true;
    return state;
}

Manager::CollectionMode Manager::GetCollectionMode(
    const rapidjson::Document& a_doc,
    const char* a_modeMember,
    const char* a_collectionMember) {
    if (a_doc.HasMember(a_modeMember) && a_doc[a_modeMember].IsString()) {
        const std::string_view mode = a_doc[a_modeMember].GetString();
        if (mode == "inherit") return CollectionMode::kInherit;
        if (mode == "replaceBase") return CollectionMode::kReplaceBase;
    }

    // Legacy presets did not have an explicit mode. A populated array keeps its
    // former intent, while an empty array is treated as "do not touch" to avoid
    // destructive upgrades.
    if (a_doc.HasMember(a_collectionMember) &&
        a_doc[a_collectionMember].IsArray() &&
        !a_doc[a_collectionMember].Empty()) {
        return CollectionMode::kReplaceBase;
    }

    return CollectionMode::kUnspecified;
}

void Manager::ReconcilePerks(
    RE::TESNPC* a_npc,
    const rapidjson::Document& a_doc,
    NPCCollectionState& a_state) {
    const auto mode = GetCollectionMode(a_doc, "perksMode", "perks");
    if (mode == CollectionMode::kUnspecified) return;
    if (mode == CollectionMode::kInherit && !a_state.perksManaged) return;

    std::map<RE::BGSPerk*, std::int8_t> desired;
    if (mode == CollectionMode::kInherit) {
        desired = a_state.basePerks;
    }
    else if (a_doc.HasMember("perks") && a_doc["perks"].IsArray()) {
        for (const auto& entry : a_doc["perks"].GetArray()) {
            if (!entry.IsObject() ||
                !entry.HasMember("form") || !entry["form"].IsString() ||
                !entry.HasMember("rank") || !entry["rank"].IsInt()) {
                continue;
            }

            if (auto perk = RE::TESForm::LookupByID<RE::BGSPerk>(
                    FormUtil::FormIDFromString(entry["form"].GetString()))) {
                desired[perk] = static_cast<std::int8_t>(entry["rank"].GetInt());
            }
        }
    }

    std::set<RE::BGSPerk*> owned;
    for (const auto& [perk, rank] : a_state.basePerks) {
        (void)rank;
        owned.insert(perk);
    }
    for (const auto& [perk, rank] : a_state.appliedPerks) {
        (void)rank;
        owned.insert(perk);
    }

    std::vector<RE::BGSPerk*> toRemove;
    for (std::uint32_t i = 0; i < a_npc->perkCount; i++) {
        auto* perk = a_npc->perks ? a_npc->perks[i].perk : nullptr;
        if (perk && owned.contains(perk) && !desired.contains(perk)) {
            toRemove.push_back(perk);
        }
    }
    if (!toRemove.empty()) {
        a_npc->RemovePerks(toRemove);
    }

    for (const auto& [perk, rank] : desired) {
        bool found = false;
        for (std::uint32_t i = 0; i < a_npc->perkCount; i++) {
            if (a_npc->perks && a_npc->perks[i].perk == perk) {
                a_npc->perks[i].currentRank = rank;
                found = true;
                break;
            }
        }
        if (!found) {
            a_npc->AddPerk(perk, rank);
        }
    }

    if (mode == CollectionMode::kReplaceBase) {
        a_state.appliedPerks = std::move(desired);
        a_state.perksManaged = true;
    }
    else {
        a_state.appliedPerks.clear();
        a_state.perksManaged = false;
    }
}

void Manager::ReconcileFactions(
    RE::TESNPC* a_npc,
    const rapidjson::Document& a_doc,
    NPCCollectionState& a_state) {
    const auto mode = GetCollectionMode(a_doc, "factionsMode", "factions");
    if (mode == CollectionMode::kUnspecified) return;
    if (mode == CollectionMode::kInherit && !a_state.factionsManaged) return;

    std::map<RE::TESFaction*, std::int8_t> desired;
    if (mode == CollectionMode::kInherit) {
        desired = a_state.baseFactions;
    }
    else if (a_doc.HasMember("factions") && a_doc["factions"].IsArray()) {
        for (const auto& entry : a_doc["factions"].GetArray()) {
            if (!entry.IsObject() ||
                !entry.HasMember("form") || !entry["form"].IsString() ||
                !entry.HasMember("rank") || !entry["rank"].IsInt()) {
                continue;
            }

            if (auto faction = RE::TESForm::LookupByID<RE::TESFaction>(
                    FormUtil::FormIDFromString(entry["form"].GetString()))) {
                desired[faction] = static_cast<std::int8_t>(entry["rank"].GetInt());
            }
        }
    }

    std::set<RE::TESFaction*> owned;
    for (const auto& [faction, rank] : a_state.baseFactions) {
        (void)rank;
        owned.insert(faction);
    }
    for (const auto& [faction, rank] : a_state.appliedFactions) {
        (void)rank;
        owned.insert(faction);
    }

    for (auto it = a_npc->factions.begin(); it != a_npc->factions.end();) {
        if (it->faction && owned.contains(it->faction) && !desired.contains(it->faction)) {
            it = a_npc->factions.erase(it);
        }
        else {
            ++it;
        }
    }

    for (const auto& [faction, rank] : desired) {
        auto current = std::find_if(
            a_npc->factions.begin(),
            a_npc->factions.end(),
            [faction](const auto& entry) { return entry.faction == faction; });
        if (current != a_npc->factions.end()) {
            current->rank = rank;
        }
        else {
            RE::FACTION_RANK entry;
            entry.faction = faction;
            entry.rank = rank;
            a_npc->factions.push_back(entry);
        }
    }

    if (mode == CollectionMode::kReplaceBase) {
        a_state.appliedFactions = std::move(desired);
        a_state.factionsManaged = true;
    }
    else {
        a_state.appliedFactions.clear();
        a_state.factionsManaged = false;
    }
}

void Manager::ReconcileSpells(
    RE::TESNPC* a_npc,
    const rapidjson::Document& a_doc,
    NPCCollectionState& a_state) {
    const auto mode = GetCollectionMode(a_doc, "spellsMode", "spells");
    if (mode == CollectionMode::kUnspecified) return;
    if (mode == CollectionMode::kInherit && !a_state.spellsManaged) return;

    std::set<RE::SpellItem*> desired;
    if (mode == CollectionMode::kInherit) {
        desired = a_state.baseSpells;
    }
    else if (a_doc.HasMember("spells") && a_doc["spells"].IsArray()) {
        for (const auto& entry : a_doc["spells"].GetArray()) {
            if (!entry.IsString()) continue;
            if (auto spell = RE::TESForm::LookupByID<RE::SpellItem>(
                    FormUtil::FormIDFromString(entry.GetString()))) {
                desired.insert(spell);
            }
        }
    }

    std::set<RE::SpellItem*> owned = a_state.baseSpells;
    owned.insert(a_state.appliedSpells.begin(), a_state.appliedSpells.end());

    auto spellList = static_cast<RE::TESSpellList*>(a_npc);
    if (spellList->actorEffects) {
        std::vector<RE::SpellItem*> toRemove;
        for (std::uint32_t i = 0; i < spellList->actorEffects->numSpells; i++) {
            auto* spell = spellList->actorEffects->spells ? spellList->actorEffects->spells[i] : nullptr;
            if (spell && owned.contains(spell) && !desired.contains(spell)) {
                toRemove.push_back(spell);
            }
        }
        if (!toRemove.empty()) {
            spellList->actorEffects->RemoveSpells(toRemove);
        }
    }

    if (!desired.empty() && !spellList->actorEffects) {
        spellList->actorEffects = new RE::TESSpellList::SpellData();
    }
    if (spellList->actorEffects) {
        for (auto* spell : desired) {
            spellList->actorEffects->AddSpell(spell);
        }
    }

    if (mode == CollectionMode::kReplaceBase) {
        a_state.appliedSpells = std::move(desired);
        a_state.spellsManaged = true;
    }
    else {
        a_state.appliedSpells.clear();
        a_state.spellsManaged = false;
    }
}

void Manager::ApplyNPCCustomizationFromJSON(RE::TESNPC* a_npc, const rapidjson::Document& doc) {
    if (!a_npc || !doc.IsObject()) return;

    auto* manager = GetSingleton();
    auto& collectionState = manager->GetOrCaptureCollectionState(a_npc);

    // --- Atributos base ---
    if (doc.HasMember("health") && doc["health"].IsFloat()) a_npc->playerSkills.health = static_cast<std::uint16_t>(doc["health"].GetFloat());
    if (doc.HasMember("magicka") && doc["magicka"].IsFloat()) a_npc->playerSkills.magicka = static_cast<std::uint16_t>(doc["magicka"].GetFloat());
    if (doc.HasMember("stamina") && doc["stamina"].IsFloat()) a_npc->playerSkills.stamina = static_cast<std::uint16_t>(doc["stamina"].GetFloat());

    // --- Offsets e Configurações (ACBS) ---
    if (doc.HasMember("healthOffset") && doc["healthOffset"].IsInt()) a_npc->actorData.healthOffset = static_cast<std::int16_t>(doc["healthOffset"].GetInt());
    if (doc.HasMember("magickaOffset") && doc["magickaOffset"].IsInt()) a_npc->actorData.magickaOffset = static_cast<std::int16_t>(doc["magickaOffset"].GetInt());
    if (doc.HasMember("staminaOffset") && doc["staminaOffset"].IsInt()) a_npc->actorData.staminaOffset = static_cast<std::int16_t>(doc["staminaOffset"].GetInt());

    if (doc.HasMember("calcMinLevel") && doc["calcMinLevel"].IsUint()) a_npc->actorData.calcLevelMin = static_cast<std::uint16_t>(doc["calcMinLevel"].GetUint());
    if (doc.HasMember("calcMaxLevel") && doc["calcMaxLevel"].IsUint()) a_npc->actorData.calcLevelMax = static_cast<std::uint16_t>(doc["calcMaxLevel"].GetUint());
    if (doc.HasMember("level") && doc["level"].IsUint()) a_npc->actorData.level = static_cast<std::uint16_t>(doc["level"].GetUint());
    if (doc.HasMember("speedMult") && doc["speedMult"].IsUint()) a_npc->actorData.speedMult = static_cast<std::uint16_t>(doc["speedMult"].GetUint());
    if (doc.HasMember("dispositionBase") && doc["dispositionBase"].IsUint()) a_npc->actorData.baseDisposition = static_cast<std::uint16_t>(doc["dispositionBase"].GetUint());
    if (doc.HasMember("bleedoutOverride") && doc["bleedoutOverride"].IsInt()) a_npc->actorData.bleedoutOverride = static_cast<std::int16_t>(doc["bleedoutOverride"].GetInt());

    // --- Flags ---
    auto& flags = a_npc->actorData.actorBaseFlags;
    if (doc.HasMember("isEssential") && doc["isEssential"].IsBool()) { doc["isEssential"].GetBool() ? flags.set(RE::ACTOR_BASE_DATA::Flag::kEssential) : flags.reset(RE::ACTOR_BASE_DATA::Flag::kEssential); }
    if (doc.HasMember("isProtected") && doc["isProtected"].IsBool()) { doc["isProtected"].GetBool() ? flags.set(RE::ACTOR_BASE_DATA::Flag::kProtected) : flags.reset(RE::ACTOR_BASE_DATA::Flag::kProtected); }
    if (doc.HasMember("isUnique") && doc["isUnique"].IsBool()) { doc["isUnique"].GetBool() ? flags.set(RE::ACTOR_BASE_DATA::Flag::kUnique) : flags.reset(RE::ACTOR_BASE_DATA::Flag::kUnique); }
    if (doc.HasMember("calcStats") && doc["calcStats"].IsBool()) { doc["calcStats"].GetBool() ? flags.set(RE::ACTOR_BASE_DATA::Flag::kPCLevelMult) : flags.reset(RE::ACTOR_BASE_DATA::Flag::kPCLevelMult); }
    if (doc.HasMember("doesntAffectStealthMeter") && doc["doesntAffectStealthMeter"].IsBool()) {
        doc["doesntAffectStealthMeter"].GetBool() ? flags.set(RE::ACTOR_BASE_DATA::Flag::kDoesntAffectStealthMeter) : flags.reset(RE::ACTOR_BASE_DATA::Flag::kDoesntAffectStealthMeter);
    }
    // --- Forms ---
    if (doc.HasMember("class") && doc["class"].IsString()) {
        a_npc->npcClass = RE::TESForm::LookupByID<RE::TESClass>(FormUtil::FormIDFromString(doc["class"].GetString()));
    }
    if (doc.HasMember("combatStyle") && doc["combatStyle"].IsString()) {
        a_npc->combatStyle = RE::TESForm::LookupByID<RE::TESCombatStyle>(FormUtil::FormIDFromString(doc["combatStyle"].GetString()));
    }

    // --- Skills (Base e Offsets) ---
    if (doc.HasMember("skills") && doc["skills"].IsArray()) {
        auto arr = doc["skills"].GetArray();
        for (rapidjson::SizeType i = 0; i < arr.Size() && i < 18; i++) {
            a_npc->playerSkills.values[i] = static_cast<std::uint8_t>(arr[i].GetInt());
        }
    }
    if (doc.HasMember("skillOffsets") && doc["skillOffsets"].IsArray()) {
        auto arr = doc["skillOffsets"].GetArray();
        for (rapidjson::SizeType i = 0; i < arr.Size() && i < 18; i++) {
            a_npc->playerSkills.offsets[i] = static_cast<std::uint8_t>(arr[i].GetInt());
        }
    }

    // Collections use an ownership-aware reconciliation. Entries that were not
    // present in the captured base state and were not applied by this plugin are
    // considered external and are preserved.
    manager->ReconcileFactions(a_npc, doc, collectionState);
    manager->ReconcilePerks(a_npc, doc, collectionState);
    manager->ReconcileSpells(a_npc, doc, collectionState);
}

void Manager::ApplyActorCustomizationFromJSON(RE::Actor* a_actor, const rapidjson::Document& doc) {
    if (!a_actor || !doc.IsObject()) return;
    auto avOwner = a_actor->AsActorValueOwner();
    if (!avOwner) return;

    if (doc.HasMember("attackDamageMult") && doc["attackDamageMult"].IsFloat())
        avOwner->SetActorValue(RE::ActorValue::kAttackDamageMult, doc["attackDamageMult"].GetFloat());
    if (doc.HasMember("healRateMult") && doc["healRateMult"].IsFloat())
        avOwner->SetActorValue(RE::ActorValue::kHealRateMult, doc["healRateMult"].GetFloat());
    if (doc.HasMember("magickaRateMult") && doc["magickaRateMult"].IsFloat())
        avOwner->SetActorValue(RE::ActorValue::kMagickaRateMult, doc["magickaRateMult"].GetFloat());
    if (doc.HasMember("staminaRateMult") && doc["staminaRateMult"].IsFloat())
        avOwner->SetActorValue(RE::ActorValue::kStaminaRateMult, doc["staminaRateMult"].GetFloat());
}

void Manager::CacheActorCustomization(
    RE::FormID baseID,
    const rapidjson::Document& doc) {
    if (!doc.IsObject()) {
        RemoveActorCustomization(baseID);
        return;
    }

    ActorRuntimeValues values;
    bool hasRuntimeValues = false;
    const auto readValue =
        [&doc, &hasRuntimeValues](
            const char* member,
            float& destination) {
            if (!doc.HasMember(member) ||
                !doc[member].IsNumber()) {
                return;
            }
            destination = doc[member].GetFloat();
            hasRuntimeValues = true;
        };

    readValue("attackDamageMult", values.attackDamageMult);
    readValue("healRateMult", values.healRateMult);
    readValue("magickaRateMult", values.magickaRateMult);
    readValue("staminaRateMult", values.staminaRateMult);

    if (!hasRuntimeValues) {
        RemoveActorCustomization(baseID);
        return;
    }

    _actorRuntimeValues[baseID] = values;
}

void Manager::RemoveActorCustomization(RE::FormID baseID) {
    _actorRuntimeValues.erase(baseID);
}

bool Manager::ApplyCachedActorCustomization(RE::Actor* actor) {
    if (!actor) return false;
    auto* base = actor->GetActorBase();
    if (!base) return false;

    const auto values =
        _actorRuntimeValues.find(base->GetFormID());
    if (values == _actorRuntimeValues.end()) return false;

    auto* actorValueOwner = actor->AsActorValueOwner();
    if (!actorValueOwner) return false;

    actorValueOwner->SetActorValue(
        RE::ActorValue::kAttackDamageMult,
        values->second.attackDamageMult);
    actorValueOwner->SetActorValue(
        RE::ActorValue::kHealRateMult,
        values->second.healRateMult);
    actorValueOwner->SetActorValue(
        RE::ActorValue::kMagickaRateMult,
        values->second.magickaRateMult);
    actorValueOwner->SetActorValue(
        RE::ActorValue::kStaminaRateMult,
        values->second.staminaRateMult);
    return true;
}

void Manager::ApplyCachedActorCustomizationsToLoadedActors() {
    auto* processLists = RE::ProcessLists::GetSingleton();
    if (!processLists) return;

    processLists->ForAllActors(
        [this](RE::Actor* actor) {
            ApplyCachedActorCustomization(actor);
            return RE::BSContainer::ForEachResult::kContinue;
        });
}
