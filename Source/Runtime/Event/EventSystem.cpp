#include "EventSystem.hpp"

namespace engine {

    void EventSystem::Init() {
        // initialisation code (not quite needed). Map is already default constructed
    }

    void EventSystem::Update(float /*dt*/) {
        std::lock_guard<std::mutex> lock(m_QueueMutex);
        
        // swap queues so new events can be queued during the dispatching phase
        m_EventQueueSwap = std::move(m_EventQueue);
        m_EventQueue.clear();


        // dispatch all deferred events for this frame
        for (auto& eventPtr : m_EventQueueSwap) {
            Dispatch(*eventPtr);
        }
        
        // clear swapped queue to destroy the unique_ptrs
        m_EventQueueSwap.clear();


    }

    void EventSystem::Shutdown() {
        m_Subscribers.clear();
        
        std::lock_guard<std::mutex> lock(m_QueueMutex);
        m_EventQueue.clear();
        m_EventQueueSwap.clear();

    }

    void EventSystem::Subscribe(EventType type, EventCallback callback) {
        m_Subscribers[type].push_back(callback);
    }

    void EventSystem::Dispatch(Event& event) {
        // if an event was handled by a previous listener, stop propagation
        if (event.Handled) return;

        EventType type = event.GetType();
        auto it = m_Subscribers.find(type);
        if (it != m_Subscribers.end()) {
            for (auto& callback : it->second) {

                callback(event);
                // system allows listeners to consume events
                if (event.Handled) break;

            }
        }
    }

    void EventSystem::QueueEvent(std::unique_ptr<Event> event) {
        std::lock_guard<std::mutex> lock(m_QueueMutex);
        m_EventQueue.push_back(std::move(event));
    }

} // namespace engine
