/*
Copyright(c) 2015-2026 Panos Karabelas
Licensed under the Spartan Engine License. See license.md in the repository root.
https://github.com/PanosK92/SpartanEngine/blob/master/license.md
Commercial use requires written permission and negotiated payment terms.
*/

#include "pch.h"
#include "CarService.h"
#include "Physics.h"
#include "../Entity.h"
#include "../../car/Car.h"
#include "../../car/CarDirt.h"
#include "../../car/CarHud.h"
#include "../../input/Input.h"
#include "../../core/Engine.h"
#include "../../core/Timer.h"
SP_WARNINGS_OFF
#include "../../io/pugixml.hpp"
SP_WARNINGS_ON

using namespace std;
using namespace spartan::math;

namespace spartan
{
    namespace
    {
        const float max_service_speed = 3.0f / 3.6f; // the car has to be parked, not rolling through
        const float done_hold         = 3.0f;
    }

    CarService::CarService(Entity* entity) : Component(entity)
    {
        SP_REGISTER_ATTRIBUTE_GET_SET(GetShopName, SetShopName, std::string);
        SP_REGISTER_ATTRIBUTE_GET_SET(GetBayExtents, SetBayExtents, Vector3);
        SP_REGISTER_ATTRIBUTE_GET_SET(GetWashDuration, SetWashDuration, float);
    }

    void CarService::Start()
    {
        m_state    = State::Idle;
        m_timer    = 0.0f;
        m_customer = nullptr;
    }

    void CarService::Stop()
    {
        Start();
    }

    void CarService::SetBayExtents(const Vector3& extents)
    {
        m_bay_extents = Vector3(max(extents.x, 0.5f), max(extents.y, 0.5f), max(extents.z, 0.5f));
    }

    void CarService::SetWashDuration(float seconds)
    {
        m_wash_duration = isfinite(seconds) ? clamp(seconds, 0.0f, 60.0f) : 4.0f;
    }

    Car* CarService::FindCustomer() const
    {
        const Vector3 bay        = GetEntity()->GetPosition();
        const Quaternion inverse = GetEntity()->GetRotation().Inverse();
        for (Car* car : Car::GetAll())
        {
            Entity* root = car ? car->GetRootEntity() : nullptr;
            if (!root || !car->IsOccupied())
                continue;

            const Vector3 local = inverse * (root->GetPosition() - bay);
            if (abs(local.x) <= m_bay_extents.x && abs(local.y) <= m_bay_extents.y && abs(local.z) <= m_bay_extents.z)
                return car;
        }
        return nullptr;
    }

    bool CarService::Wash()
    {
        Car* customer = FindCustomer();
        if (!customer || m_state == State::Washing)
            return false;

        m_customer = customer;
        m_state    = State::Washing;
        m_timer    = 0.0f;
        return true;
    }

    void CarService::Tick()
    {
        if (!Engine::IsFlagSet(EngineMode::Playing))
            return;

        const float delta_time = static_cast<float>(Timer::GetDeltaTimeSec());
        Car* customer          = FindCustomer();

        // driving off mid wash walks away from the service, the dirt stays
        if (m_state == State::Washing && customer != m_customer)
        {
            m_state = State::Idle;
        }
        m_customer = customer;
        if (!m_customer)
        {
            m_state = State::Idle;
            return;
        }

        Physics* physics  = m_customer->GetRootEntity()->GetComponent<Physics>();
        const bool parked = !physics || physics->GetLinearVelocity().Length() <= max_service_speed;

        if (m_state == State::Washing)
        {
            m_timer += delta_time;
            if (m_timer >= m_wash_duration)
            {
                CarDirt::Wash();
                m_state = State::Done;
                m_timer = 0.0f;
            }
        }
        else if (m_state == State::Done)
        {
            m_timer += delta_time;
            if (m_timer >= done_hold)
            {
                m_state = State::Idle;
            }
        }

        if (!parked && m_state != State::Washing)
            return;

        char detail[32];
        snprintf(detail, sizeof(detail), "DIRT %d%%", static_cast<int>(CarDirt::GetCoverage() * 100.0f + 0.5f));
        car_hud::service_row rows[] =
        {
            { "Car Wash", detail, m_state != State::Washing }
        };

        car_hud::service_menu menu;
        menu.shop      = m_shop_name.c_str();
        menu.rows      = rows;
        menu.row_count = 1;
        if (m_state == State::Washing)
        {
            menu.status   = "WASHING";
            menu.progress = m_wash_duration > 0.0f ? m_timer / m_wash_duration : 1.0f;
        }
        else if (m_state == State::Done)
        {
            menu.status = "YOUR CAR IS CLEAN";
        }
        else
        {
            menu.status = "ENTER OR CLICK TO WASH";
        }

        const int chosen = car_hud::draw_service_menu(menu);
        if (m_state == State::Idle && (chosen == 0 || Input::GetKeyDown(KeyCode::Enter)))
        {
            Wash();
        }
    }

    void CarService::Save(pugi::xml_node& node)
    {
        node.append_attribute("shop_name")     = m_shop_name.c_str();
        node.append_attribute("bay_extents_x") = m_bay_extents.x;
        node.append_attribute("bay_extents_y") = m_bay_extents.y;
        node.append_attribute("bay_extents_z") = m_bay_extents.z;
        node.append_attribute("wash_duration") = m_wash_duration;
    }

    void CarService::Load(pugi::xml_node& node)
    {
        m_shop_name = node.attribute("shop_name").as_string("Car Wash");
        SetBayExtents(Vector3(node.attribute("bay_extents_x").as_float(2.5f), node.attribute("bay_extents_y").as_float(2.5f), node.attribute("bay_extents_z").as_float(4.5f)));
        SetWashDuration(node.attribute("wash_duration").as_float(4.0f));
    }
}
