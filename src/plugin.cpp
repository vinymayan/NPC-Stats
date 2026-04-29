#include "logger.h"
#include "Settings.h"
#include "Manager.h"
#include "Hooks.h"

void OnMessage(SKSE::MessagingInterface::Message* message) {
    if (message->type == SKSE::MessagingInterface::kDataLoaded) {
        logger::info("[Plugin] Data loaded. Initializing NPC Stats Replacer...");
        NSettings::MmRegister();
        Manager::GetSingleton()->PopulateAllLists();
        NSettings::Load();
		Load3DHook::Install();
    }
    if (message->type == SKSE::MessagingInterface::kNewGame || message->type == SKSE::MessagingInterface::kPostLoadGame) {
    }
}

SKSEPluginLoad(const SKSE::LoadInterface* skse) {
    SetupLog();
    logger::info("Plugin loaded");
    SKSE::Init(skse);
    SKSE::GetMessagingInterface()->RegisterListener(OnMessage);
    return true;
}