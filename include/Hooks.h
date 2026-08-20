#pragma once

class ActorLoadEventHandler :
    public RE::BSTEventSink<RE::TESObjectLoadedEvent> {
public:
    static ActorLoadEventHandler* GetSingleton() {
        static ActorLoadEventHandler singleton;
        return std::addressof(singleton);
    }

    void Register();

    RE::BSEventNotifyControl ProcessEvent(
        const RE::TESObjectLoadedEvent* event,
        RE::BSTEventSource<RE::TESObjectLoadedEvent>*) override;

private:
    ActorLoadEventHandler() = default;
    bool _registered = false;
};
