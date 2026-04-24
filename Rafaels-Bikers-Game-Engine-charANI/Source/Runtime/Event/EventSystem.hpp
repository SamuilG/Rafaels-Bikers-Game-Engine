#pragma once

#include "../Core/System.h"
#include "Event.hpp"
#include <functional>
#include <vector>
#include <unordered_map>
#include <memory>
#include <mutex>

namespace engine {

    // callback signature for listeners
    // receive a generic Event reference
    using EventCallback = std::function<void(Event&)>;

    class EventSystem final : public System {
    public:
        EventSystem() = default;
        ~EventSystem() override = default;

        // System Interface implementation
        void Init() override;
        void Update(float dt) override;
        void Shutdown() override;

        // subscribe to a specific type of event
        void Subscribe(EventType type, EventCallback callback);

        // dispatch an event to all subscribers
        // (time-critical features that tie directly into the call stack)
        void Dispatch(Event& event);

        // queue an event to be dispatched asynchronously during the next Update() tick
        // (deferring events that would otherwise stall the current logic flow)
        void QueueEvent(std::unique_ptr<Event> event);



    private:
        // maps EventTypes to lists of callback functions
        std::unordered_map<EventType, std::vector<EventCallback>> m_Subscribers;


        // a queue of deferred events
        std::vector<std::unique_ptr<Event>> m_EventQueue;
        std::vector<std::unique_ptr<Event>> m_EventQueueSwap; // (prevent infinite loops during Dispatch)
        
        
        // mutex for thread safety if queues are being pushed to by multiple threads
        std::mutex m_QueueMutex;


    };

} // namespace engine
