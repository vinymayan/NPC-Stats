#include "Hooks.h"
#include "Manager.h"

void ActorLoadEventHandler::Register() {
    if (_registered) return;

    auto* eventSource = RE::ScriptEventSourceHolder::GetSingleton();
    if (!eventSource) {
        logger::error(
            "[ActorLoadEvent] ScriptEventSourceHolder is unavailable.");
        return;
    }

    eventSource->AddEventSink<RE::TESObjectLoadedEvent>(this);
    _registered = true;
    logger::info("[ActorLoadEvent] Registered TESObjectLoadedEvent sink.");
}

RE::BSEventNotifyControl ActorLoadEventHandler::ProcessEvent(
    const RE::TESObjectLoadedEvent* event,
    RE::BSTEventSource<RE::TESObjectLoadedEvent>*) {
    if (!event || !event->loaded) {
        return RE::BSEventNotifyControl::kContinue;
    }

    auto* form = RE::TESForm::LookupByID(event->formID);
    auto* actor = form ? form->As<RE::Actor>() : nullptr;
    if (actor) {
        Manager::GetSingleton()->ApplyCachedActorCustomization(actor);
    }

    return RE::BSEventNotifyControl::kContinue;
}
