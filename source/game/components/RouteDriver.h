/*
Copyright(c) 2015-2026 Panos Karabelas
Licensed under the Spartan Engine License. See license.md in the repository root.
https://github.com/PanosK92/SpartanEngine/blob/master/license.md
Commercial use requires written permission and negotiated payment terms.
*/

#pragma once

#include "../../world/components/Component.h"
#include "../../car/AiDriver.h"
#include "../../car/RacingLine.h"
#include "../../math/Vector3.h"
#include <memory>
#include <string>
#include <vector>

namespace spartan
{
    class Car;

    namespace road_traffic
    {
        class Network;
    }

    // the car on this entity (a car prefab) drives itself from a start to a finish over the road network, with the racing ai
    // start and finish are entities placed on or near a road, the route between them is the shortest one along the lanes
    // without a start the car pulls out from where it is parked and joins the nearest road, in edit mode the route is drawn
    class RouteDriver : public Component
    {
    public:
        bool StartsEarly() const override { return true; }
        RouteDriver(Entity* entity);
        ~RouteDriver() override;

        void Start() override;
        void Stop() override;
        void Remove() override;
        void Tick() override;
        void Save(pugi::xml_node& node) override;
        void Load(pugi::xml_node& node) override;

        Car* GetCar() const;
        const std::shared_ptr<RacingLine>& GetRacingLine() const { return m_line; }

    private:
        struct Route
        {
            std::vector<RacingLine::Sample> samples;
            std::vector<uint64_t> roads;
            bool from_car = false; // the route starts where the car is parked
        };

        bool EnsureNetwork(bool rebuild);
        bool BuildRoute(Car* car, Route& route);
        void Launch(Car* car);
        void TickPreview();

        // settings
        uint64_t m_start_entity_id = 0;     // where the route starts, 0 pulls out from where the car is parked
        uint64_t m_end_entity_id   = 0;     // where the route ends, the car stops on the road there
        float m_skill              = 1.0f;  // 0 to 1, how much of the measured grip and braking the driver dares to use
        float m_max_speed          = 95.0f; // m/s
        float m_edge_margin        = 1.7f;  // meters the racing line keeps between the car's center and the lane edge
        float m_start_delay        = 2.0f;  // seconds on the handbrake before setting off
        bool m_verbose             = false;

        // the driver, readable through component_get
        AiDriverStats m_stats;
        float m_route_length = 0.0f;
        std::string m_status = "idle";

        std::shared_ptr<RacingLine> m_line;
        std::shared_ptr<road_traffic::Network> m_network;
        Car* m_car           = nullptr;
        bool m_launched      = false;
        float m_retry_time   = 0.0f;

        // edit mode preview of the route
        std::vector<math::Vector3> m_preview;
        math::Vector3 m_preview_key[3];
        float m_preview_time = 0.0f;
    };
}
