#pragma once

#include <string>
#include <vector>
#include <map>
#include <set>
#include <functional>
#include "ClibUtil/editorID.hpp"
#include "rapidjson/document.h"
#include "rapidjson/filereadstream.h"
#include "rapidjson/filewritestream.h"
#include "rapidjson/prettywriter.h"
#include "rapidjson/stringbuffer.h"

namespace FormUtil {
    const RE::TESFile* GetMasterFile(RE::TESForm* ref);
    std::string NormalizeFormID(RE::TESForm* form);
    RE::FormID FormIDFromString(const std::string& str);
}

struct InternalFormInfo {
    RE::FormID formID;
    std::string editorID;
    std::string name;
    std::string pluginName;
    std::string formType;
    std::string cachedDisplayName; 

    void UpdateDisplayName() {
        std::string base = !name.empty() ? name : (!editorID.empty() ? editorID : "Unknown");
        cachedDisplayName = std::format("{} [{:08X}]", base, formID);
    }
    // Helper for UI
    std::string GetDisplayName() const {
        if (!name.empty()) return name;
        if (!editorID.empty()) return editorID;
        return std::to_string(formID);
    }
};

class Manager {
public:
    static Manager* GetSingleton() {
        static Manager singleton;
        return &singleton;
    }

    void PopulateAllLists();
    void RefreshLists(std::string_view a_signatures);
    static std::string ToUTF8(std::string_view a_str);
    // Data Store: Map of "TypeName" -> List of InternalFormInfo
    // We use this to feed the UI
    const std::vector<InternalFormInfo>& GetList(const std::string& typeName);

    // Register callback for when population is done
    void RegisterReadyCallback(std::function<void()> callback);
    static void ApplyNPCCustomizationFromJSON(RE::TESNPC* a_npc, const rapidjson::Document& doc);
    static void ApplyActorCustomizationFromJSON(RE::Actor* a_actor, const rapidjson::Document& doc);
    void CacheActorCustomization(RE::FormID baseID, const rapidjson::Document& doc);
    void RemoveActorCustomization(RE::FormID baseID);
    bool ApplyCachedActorCustomization(RE::Actor* actor);
    void ApplyCachedActorCustomizationsToLoadedActors();

    bool _isPopulated = false;

private:
    enum class CollectionMode {
        kUnspecified,
        kInherit,
        kReplaceBase
    };

    struct NPCCollectionState {
        bool captured = false;

        std::map<RE::BGSPerk*, std::int8_t> basePerks;
        std::map<RE::TESFaction*, std::int8_t> baseFactions;
        std::set<RE::SpellItem*> baseSpells;

        std::map<RE::BGSPerk*, std::int8_t> appliedPerks;
        std::map<RE::TESFaction*, std::int8_t> appliedFactions;
        std::set<RE::SpellItem*> appliedSpells;

        bool perksManaged = false;
        bool factionsManaged = false;
        bool spellsManaged = false;
    };

    struct ActorRuntimeValues {
        float attackDamageMult = 1.0f;
        float healRateMult = 100.0f;
        float magickaRateMult = 100.0f;
        float staminaRateMult = 100.0f;
    };

    Manager() = default;

    template <typename T>
    void PopulateList(const std::string& a_typeName, std::function<bool(T*)> a_filter = nullptr);

    NPCCollectionState& GetOrCaptureCollectionState(RE::TESNPC* a_npc);
    static CollectionMode GetCollectionMode(
        const rapidjson::Document& a_doc,
        const char* a_modeMember,
        const char* a_collectionMember);
    void ReconcilePerks(RE::TESNPC* a_npc, const rapidjson::Document& a_doc, NPCCollectionState& a_state);
    void ReconcileFactions(RE::TESNPC* a_npc, const rapidjson::Document& a_doc, NPCCollectionState& a_state);
    void ReconcileSpells(RE::TESNPC* a_npc, const rapidjson::Document& a_doc, NPCCollectionState& a_state);

    std::map<std::string, std::vector<InternalFormInfo>> _dataStore;
    std::vector<std::function<void()>> _readyCallbacks;
    std::map<RE::FormID, NPCCollectionState> _npcCollectionStates;
    std::map<RE::FormID, ActorRuntimeValues> _actorRuntimeValues;
};

