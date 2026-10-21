/*
Copyright(c) 2015-2026 Panos Karabelas
Licensed under the Spartan Engine License. See license.md in the repository root.
https://github.com/PanosK92/SpartanEngine/blob/master/license.md
Commercial use requires written permission and negotiated payment terms.
*/

#pragma once

#include "Component.h"
#include "../../math/Vector3.h"
#include <string>

namespace spartan
{
    class Car;

    // a service bay, gran turismo style: stop the car you drive inside the box and the shop offers its services
    class CarService : public Component
    {
    public:
        CarService(Entity* entity);
        ~CarService() = default;

        void Start() override;
        void Stop() override;
        void Tick() override;
        void Save(pugi::xml_node& node) override;
        void Load(pugi::xml_node& node) override;

        const std::string& GetShopName() const { return m_shop_name; }
        void SetShopName(const std::string& name) { m_shop_name = name; }

        // half sizes of the bay in the entity's space, unscaled
        const math::Vector3& GetBayExtents() const { return m_bay_extents; }
        void SetBayExtents(const math::Vector3& extents);

        float GetWashDuration() const { return m_wash_duration; }
        void SetWashDuration(float seconds);

        // starts the wash when the driven car waits in the bay
        bool Wash();
        bool IsCarInBay() const { return m_customer != nullptr; }

    private:
        enum class State
        {
            Idle,
            Washing,
            Done
        };

        Car* FindCustomer() const;

        std::string m_shop_name     = "Car Wash";
        math::Vector3 m_bay_extents = math::Vector3(2.5f, 2.5f, 4.5f);
        float m_wash_duration       = 4.0f;

        State m_state     = State::Idle;
        float m_timer     = 0.0f;
        Car* m_customer   = nullptr;
    };
}
