#pragma once

#include <string>
#include <memory>
#include <any>
#include <glm/glm.hpp>

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
        ItemCollected,      // single item
        AllItemsCollected,  // all items
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
        CollisionEvent(const std::string& entityA, const std::string& entityB,
                       float relativeSpeed = 0.0f,
                       glm::vec3 contactNormal = glm::vec3(0.0f),
                       glm::vec3 contactPoint = glm::vec3(0.0f),
                       bool hasContactPoint = false)
            : m_EntityA(entityA), m_EntityB(entityB),
              m_RelativeSpeed(relativeSpeed), m_ContactNormal(contactNormal),
              m_ContactPoint(contactPoint), m_HasContactPoint(hasContactPoint) {}

        const std::string& GetEntityA() const { return m_EntityA; }
        const std::string& GetEntityB() const { return m_EntityB; }

        // Speed of approach along the contact normal (m/s, always >= 0)
        float GetRelativeSpeed() const { return m_RelativeSpeed; }
        // World-space contact normal (from body2 surface toward body1)
        const glm::vec3& GetContactNormal() const { return m_ContactNormal; }
        // Average world-space contact point when supplied by the physics backend.
        const glm::vec3& GetContactPoint() const { return m_ContactPoint; }
        bool HasContactPoint() const { return m_HasContactPoint; }

        EventType GetType() const override { return EventType::Collision; }
        const char* GetName() const override { return "CollisionEvent"; }

    private:
        std::string m_EntityA;
        std::string m_EntityB;
        float       m_RelativeSpeed  = 0.0f;
        glm::vec3   m_ContactNormal  = glm::vec3(0.0f);
        glm::vec3   m_ContactPoint   = glm::vec3(0.0f);
        bool        m_HasContactPoint = false;
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



    class ItemCollectedEvent : public Event {
    public:
        ItemCollectedEvent(int itemIndex, int currentTotal)
            : m_ItemIndex(itemIndex), m_CurrentTotal(currentTotal) {}

        int GetItemIndex()    const { return m_ItemIndex; }    
        int GetCurrentTotal() const { return m_CurrentTotal; } 

        EventType GetType() const override { return EventType::ItemCollected; }
        const char* GetName() const override { return "ItemCollectedEvent"; }

    private:
        int m_ItemIndex;
        int m_CurrentTotal;
    };

    class AllItemsCollectedEvent : public Event {
    public:
        EventType GetType() const override { return EventType::AllItemsCollected; }
        const char* GetName() const override { return "AllItemsCollectedEvent"; }
    };


} // namespace engine
