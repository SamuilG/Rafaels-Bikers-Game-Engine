#pragma once

namespace engine {

    class System {
    public:
        virtual ~System() = default;

        virtual void Init() = 0;
        virtual void Update(float dt) = 0;
        virtual void Shutdown() = 0;
    };

}