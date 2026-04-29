#include "Hooks.h"
#include "Manager.h"

// ==============================================================================
// SISTEMA DE AGENDAMENTO (POLLING PARA 3D CARREGADO)
// ==============================================================================
namespace TaskScheduler {

    static void WaitFor3DAndApplyLoad(RE::FormID refID, int retries) {
        if (retries <= 0) {
            logger::warn("[TaskScheduler] Esgotaram as tentativas para carregar o 3D do FormID {:08X}.", refID);
            return;
        }

        std::thread([refID, retries]() {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));

            SKSE::GetTaskInterface()->AddTask([refID, retries]() {
                auto ref = RE::TESForm::LookupByID<RE::TESObjectREFR>(refID);
                if (!ref) {
                    logger::warn("[TaskScheduler] Referencia {:08X} deixou de existir durante a espera.", refID);
                    return;
                }

                auto node3D = ref->Get3D();
                if (node3D) {
                    logger::debug("[TaskScheduler] 3D carregado com sucesso para {:08X}! Inspecionando FMDs...", refID);

                    if (ref->GetFormType() == RE::FormType::ActorCharacter || ref->GetFormType() == RE::FormType::NPC) {
                        if (auto actor = ref->As<RE::Actor>()) {
                            if (auto base = actor->GetActorBase()) {
                                if (Manager::GetSingleton()->IsNPCAffected(base->GetFormID())) {
                                    Manager::GetSingleton()->LoadAndApplyActorCustomizations(actor);
                                }
                            }
                        }
                    }
                }
                else {
                    logger::debug("[TaskScheduler] 3D ainda ausente para {:08X}. Tentativas restantes: {}", refID, retries - 1);
                    WaitFor3DAndApplyLoad(refID, retries - 1);
                }
                });
            }).detach();
    }

}

RE::NiAVObject* Load3DHook::Hook_Load3D_REFR(RE::TESObjectREFR* a_this, bool a_backgroundLoading) {
    auto result3D = _Load3D_REFR(a_this, a_backgroundLoading);
    ProcessLoad3D(a_this, result3D);
    return result3D;
}

RE::NiAVObject* Load3DHook::Hook_Load3D_Char(RE::Character* a_this, bool a_backgroundLoading) {
    auto result3D = _Load3D_Char(a_this, a_backgroundLoading);
    ProcessLoad3D(a_this, result3D);
    return result3D;
}

RE::NiAVObject* Load3DHook::Hook_Load3D_Player(RE::PlayerCharacter* a_this, bool a_backgroundLoading) {
    auto result3D = _Load3D_Player(a_this, a_backgroundLoading);
    ProcessLoad3D(a_this, result3D);
    return result3D;
}

// Lógica unificada para qualquer objeto que termine de carregar o 3D
void Load3DHook::ProcessLoad3D(RE::TESObjectREFR* a_this, RE::NiAVObject* result3D) {
    if (result3D && a_this) {
        bool isActor = (a_this->GetFormType() == RE::FormType::ActorCharacter || a_this->GetFormType() == RE::FormType::NPC);
        const char* typeStr = isActor ? "ATOR " : "";

        if (isActor) {
            if (auto actor = a_this->As<RE::Actor>()) {
                if (auto base = actor->GetActorBase()) {
                    if (Manager::GetSingleton()->IsNPCAffected(base->GetFormID())) {
                        Manager::GetSingleton()->LoadAndApplyActorCustomizations(actor);
                    }
                }
            }
            else {
                logger::debug("[LOAD3D] Load3D foi chamado para {}RefID: {:08X}, mas o 3D ainda nao esta marcado como carregado. Agendando...", typeStr, a_this->GetFormID());
                TaskScheduler::WaitFor3DAndApplyLoad(a_this->GetFormID(), 20);
            }
        }
    }
}