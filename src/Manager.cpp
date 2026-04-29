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



void Manager::RegisterAffectedNPC(RE::FormID baseID, const std::string& nifPath) {
    _affectedNPCs[baseID] = nifPath;
}

void Manager::UnregisterAffectedNPC(RE::FormID baseID) {
    _affectedNPCs.erase(baseID);
}

bool Manager::IsNPCAffected(RE::FormID baseID) {
    auto it = _affectedNPCs.find(baseID);
    if (it != _affectedNPCs.end()) {

        return true;
    }
    return false;
}

void Manager::ApplyNPCCustomizationFromJSON(RE::TESNPC* a_npc, const rapidjson::Document& doc) {
    if (!a_npc || !doc.IsObject()) return;

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

    // --- Factions ---
    if (doc.HasMember("factions") && doc["factions"].IsArray()) {
        a_npc->factions.clear();
        for (auto& f : doc["factions"].GetArray()) {
            if (f.HasMember("form") && f.HasMember("rank")) {
                if (auto fac = RE::TESForm::LookupByID<RE::TESFaction>(FormUtil::FormIDFromString(f["form"].GetString()))) {
                    RE::FACTION_RANK fr;
                    fr.faction = fac;
                    fr.rank = static_cast<int8_t>(f["rank"].GetInt());
                    a_npc->factions.push_back(fr);
                }
            }
        }
    }

    // --- Perks ---
    if (doc.HasMember("perks") && doc["perks"].IsArray()) {
        std::vector<RE::BGSPerk*> toRemove;
        for (std::uint32_t i = 0; i < a_npc->perkCount; i++) {
            if (a_npc->perks && a_npc->perks[i].perk) toRemove.push_back(a_npc->perks[i].perk);
        }
        a_npc->RemovePerks(toRemove);

        for (auto& p : doc["perks"].GetArray()) {
            if (p.HasMember("form") && p.HasMember("rank")) {
                if (auto perk = RE::TESForm::LookupByID<RE::BGSPerk>(FormUtil::FormIDFromString(p["form"].GetString()))) {
                    a_npc->AddPerk(perk, static_cast<int8_t>(p["rank"].GetInt()));
                }
            }
        }
    }

    // --- Spells (Correção via TESSpellList / actorEffects) ---
    if (doc.HasMember("spells") && doc["spells"].IsArray()) {
        auto spellComp = static_cast<RE::TESSpellList*>(a_npc);

        // Remove os feitiços existentes da base Vacnilla
        if (spellComp->actorEffects) {
            std::vector<RE::SpellItem*> toRemove;
            for (std::uint32_t i = 0; i < spellComp->actorEffects->numSpells; i++) {
                if (spellComp->actorEffects->spells && spellComp->actorEffects->spells[i]) {
                    toRemove.push_back(spellComp->actorEffects->spells[i]);
                }
            }
            for (auto* sp : toRemove) {
                spellComp->actorEffects->RemoveSpell(sp);
            }
        }

        auto spellArray = doc["spells"].GetArray();
        if (spellArray.Size() > 0) {
            if (!spellComp->actorEffects) {
                spellComp->actorEffects = new RE::TESSpellList::SpellData();
            }

            for (auto& s : spellArray) {
                if (auto sp = RE::TESForm::LookupByID<RE::SpellItem>(FormUtil::FormIDFromString(s.GetString()))) {
                    spellComp->actorEffects->AddSpell(sp);
                }
            }
        }
    }
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

void Manager::LoadAndApplyActorCustomizations(RE::Actor* a_actor) {
    if (!a_actor) return;
    auto base = a_actor->GetActorBase();
    if (!base) return;

    std::string editorID = clib_util::editorID::get_editorID(base);
    if (editorID.empty()) editorID = std::format("{:08X}", base->GetFormID());

    std::string npcPath = std::format("Data/SKSE/Plugins/NPC Stats/NPC/{}.json", editorID);
    if (!std::filesystem::exists(npcPath)) return;

    FILE* fp = nullptr;
    fopen_s(&fp, npcPath.c_str(), "rb");
    if (!fp) return;

    char readBuffer[2048];
    rapidjson::FileReadStream is(fp, readBuffer, sizeof(readBuffer));
    rapidjson::Document doc;
    doc.ParseStream(is);
    fclose(fp);

    if (!doc.IsObject()) return;

    // Verifica se ele tem um preset lincado
    if (doc.HasMember("preset") && doc["preset"].IsString()) {
        std::string presetPath = std::format("Data/SKSE/Plugins/NPC Stats/Presets/{}.json", doc["preset"].GetString());
        FILE* pFp = nullptr;
        fopen_s(&pFp, presetPath.c_str(), "rb");
        if (pFp) {
            char pReadBuffer[2048];
            rapidjson::FileReadStream pIs(pFp, pReadBuffer, sizeof(pReadBuffer));
            rapidjson::Document pDoc;
            pDoc.ParseStream(pIs);
            fclose(pFp);
            ApplyActorCustomizationFromJSON(a_actor, pDoc);
        }
    }
    else {
        ApplyActorCustomizationFromJSON(a_actor, doc);
    }
}
