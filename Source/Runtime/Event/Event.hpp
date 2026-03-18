#pragma once

#include <string>
#include <memory>
#include <any>

namespace engine {

    // Unique Identifier for Event Types
    // (maintain type safety and avoid RTTI overhead if possible)
    enum class EventType {
        None = 0,
        WindowClose,
        WindowResize,
        KeyPressed,
        KeyReleased,
        MouseMoved,
        MouseButtonPressed,
        MouseButtonReleased,
        Collision,
        GameStateChanged,
        Custom
    };

    // Base Event Class
    class Event {
    public:
        virtual ~Event() = default;

        // abstract function
        // ensure every event can identify itself
        virtual EventType GetType() const = 0;
        
        // abstract debug name
        virtual const char* GetName() const = 0;

        // whether this event has been handled or not
        // (used to stop propagation)
        bool Handled = false;
    };

    // 
    // Specific Event Implementations
    // 

    class WindowResizeEvent : public Event {
    public:
        WindowResizeEvent(unsigned int width, unsigned int height)
            : m_Width(width), m_Height(height) {}

        unsigned int GetWidth() const { return m_Width; }
        unsigned int GetHeight() const { return m_Height; }

        EventType GetType() const override { return EventType::WindowResize; }
        const char* GetName() const override { return "WindowResizeEvent"; }

    private:
        unsigned int m_Width, m_Height;
    };


    class CollisionEvent : public Event {
    public:
        // ideally these would be Entity IDs or Physics Body IDs 
        // using strings for an abstract functional representation
        CollisionEvent(const std::string& entityA, const std::string& entityB)
            : m_EntityA(entityA), m_EntityB(entityB) {}


        const std::string& GetEntityA() const { return m_EntityA; }
        const std::string& GetEntityB() const { return m_EntityB; }

        EventType GetType() const override { return EventType::Collision; }
        const char* GetName() const override { return "CollisionEvent"; }



    private:
        std::string m_EntityA;
        std::string m_EntityB;
    };


    class CustomEvent : public Event {
    public:
        CustomEvent(const std::string& customName, std::any payload = {})
            : m_CustomName(customName), m_Payload(payload) {}


        const std::string& GetCustomName() const { return m_CustomName; }
        
        template<typename T>
        T GetPayloadAs() const {
            return std::any_cast<T>(m_Payload);
        }


        EventType GetType() const override { return EventType::Custom; }
        const char* GetName() const override { return "CustomEvent"; }

    private:
        std::string m_CustomName;
        std::any m_Payload;
        
    };

} // namespace engine
