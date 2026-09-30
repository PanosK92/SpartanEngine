/*
Copyright(c) 2015-2026 Panos Karabelas
Licensed under the Spartan Engine License. See license.md in the repository root.
https://github.com/PanosK92/SpartanEngine/blob/master/license.md
Commercial use requires written permission and negotiated payment terms.
*/

#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>
#include "../math/Vector3.h"

namespace spartan
{
    class Car;
    class Physics;
    class RacingLine;

    struct AiDriverSettings
    {
        float skill        = 1.0f;  // 0 to 1, how much of the measured grip and braking it dares to use
        float max_speed    = 95.0f; // m/s
        float launch_delay = 0.0f;  // seconds on the handbrake after taking over, a grid start
        bool learning      = true;  // relearn every corner's grip after each lap
        bool verbose       = false; // log the driver's state every second
    };

    struct AiDriverStats
    {
        uint32_t lap        = 0;
        uint32_t resets     = 0;
        float lap_time      = 0.0f;
        float last_lap_time = 0.0f;
        float best_lap_time = 0.0f;
        float speed_kmh     = 0.0f;
        float target_kmh    = 0.0f;
        float distance      = 0.0f; // along the racing line
        float travelled     = 0.0f; // meters along the line since taking over, every lap included
        float line_error    = 0.0f;
        float ideal_lap     = 0.0f; // lap time the current speed plan predicts
        float cornering_g   = 0.0f; // usable cornering grip at 30 m/s
        float braking_g     = 0.0f; // usable braking at 30 m/s
        float front_use     = 0.0f; // share of the peak force the front axle is using
        float rear_use      = 0.0f;
        float body_slip     = 0.0f; // radians between where the car points and where it goes
        bool finished       = false; // an open line was driven to its end, the car is parked there
        bool following      = false; // held back by a slower car in its path
        bool passing        = false; // off the line to get by a slower car
    };

    // racing driver that takes any car around a racing line with the same pedals and steering a player has
    // nothing about the car is assumed: the tires report the grip they could make, the driver fits it against speed (downforce) and measures
    // how much of it the chassis can use before one axle gives up, so any car, surface or tire state gets its own limits
    // hand one to a car with Car::SetAiDriver, the car owns and ticks it, and gets its controls back when it is replaced or cleared
    // on an open line (a route) it drives from where it is to the end, stops there and holds the car
    // other cars on the line are followed at a safe gap and passed where the road beside them is clear, oncoming traffic included
    class AiDriver
    {
    public:
        AiDriver(std::shared_ptr<RacingLine> line, const AiDriverSettings& settings = {});
        ~AiDriver();

        Car* GetCar() const                         { return m_car; }
        const std::shared_ptr<RacingLine>& GetLine() const { return m_line; }
        const AiDriverSettings& GetSettings() const { return m_settings; }
        const AiDriverStats& GetStats() const       { return m_stats; }
        bool IsHolding() const                      { return m_hold_time > 0.0f; } // still on the grid, waiting out the launch delay
        bool IsFinished() const                     { return m_stats.finished; }

    private:
        friend class Car;

        // what the driver believes the car can do, seeded from its spec and replaced by measurements
        struct CarModel
        {
            void Seed(Physics* physics);
            void Measure(Physics* physics, float delta_time, float speed, bool on_road);
            float CorneringLimit(float speed) const; // m/s^2 the car can corner with at this speed
            float BrakingLimit(float speed) const;   // m/s^2 the car can stop with at this speed
            float Key() const;                       // moves when the plan is out of date

            // all four tires' peak cornering force over mass, fitted as grip + grip * aero * v^2
            float grip_lateral     = 9.81f;   // m/s^2 at standstill
            float aero             = 0.0f;    // fractional grip gain per (m/s)^2
            float aero_prior       = 0.0f;    // from the car's lift coefficients, holds the fit until fast samples arrive
            float long_ratio       = 1.0f;    // peak drive and brake force over peak cornering force
            float balance          = 0.85f;   // share of the tires' total grip the chassis reaches before an axle saturates
            float brake_efficiency = 0.7f;    // share of the longitudinal grip the brakes and abs turn into deceleration
            float roll_limit       = 1000.0f; // m/s^2, tall cars tip before the tires give up
            float mass             = 1500.0f;
            float peak_slip        = 0.1f;    // slip ratio where the tires make their most braking force
            float slip_normal      = 0.08f;   // radians of body slip the car shows cornering at the limit without sliding
            float understeer       = 0.05f;   // extra steering per m/s^2 of lateral acceleration

            // tire use, smoothed like a driver feels it through the seat
            float front_use   = 0.0f;
            float rear_use    = 0.0f;
            float lateral_use = 0.0f; // cornering force over cornering peak on the busier axle

        private:
            void SolveFit();
            float fit_weight = 0.0f;
            float fit_x      = 0.0f;
            float fit_y      = 0.0f;
            float fit_xx     = 0.0f;
            float fit_xy     = 0.0f;
        };

        // per racing line point
        struct Plan
        {
            float speed      = 0.0f; // target in m/s
            float grip_scale = 1.0f; // learned, scales the cornering grip here
            bool trouble     = false; // this lap the car slid, ran wide or left the road here
        };

        // the controls the driver changes, handed back as they were
        struct Handback
        {
            bool externally_controlled     = false;
            bool manual_transmission       = false;
            bool brake_reverse             = true;
            bool abs                       = true;
            bool tc                        = true;
            bool yaw_control               = true;
            float steering_speed_reduction = 0.0f;
        };

        // another car as the driver sees it, velocity from how it moved since traffic far from the camera is moved without physics
        struct TrafficCar
        {
            math::Vector3 position;
            math::Vector3 velocity;
        };

        // the closest car in the path the driver is taking
        struct Blocker
        {
            bool found  = false;
            float gap   = 0.0f; // bumper to bumper, meters
            float speed = 0.0f; // along the line, m/s, negative when it comes the other way
        };

        void Possess(Car* car);
        void Release();
        void Tick(float delta_time);
        void WatchTraffic(float delta_time, float speed, float plan_speed);

        Physics* GetPhysics() const;
        float Margin() const;
        float SpeedAt(float distance) const;
        void BuildPlan();
        void PlaceOnLine(size_t index);
        void CompleteLap();

        std::shared_ptr<RacingLine> m_line;
        AiDriverSettings m_settings;
        AiDriverStats m_stats;
        CarModel m_model;
        Handback m_handback;
        std::vector<Plan> m_plan;
        Car* m_car = nullptr;
        std::string m_name;

        // where the car is
        size_t m_index        = 0;
        float m_distance      = 0.0f;
        float m_line_error    = 0.0f;
        float m_center_offset = 0.0f;
        float m_yaw_rate      = 0.0f;

        // controls
        float m_steering      = 0.0f;
        float m_throttle      = 0.0f;
        float m_line_integral = 0.0f;
        float m_brake_limit   = 1.0f; // most pedal the tires take before a wheel locks, found by feel like threshold braking
        float m_hold_time     = 0.0f;
        bool m_timing         = false; // the lap in progress started at the line

        // traffic
        std::unordered_map<const Car*, TrafficCar> m_traffic;
        Blocker m_blocker;
        float m_pass_target  = 0.0f; // meters right of the racing line the car wants to drive at, 0 is on the line
        float m_pass_offset  = 0.0f; // where it drives now, eased toward the target
        float m_pass_rate    = 0.0f; // m/s the offset moves at
        float m_blocked_time = 0.0f; // standing behind a car, a queue is not being stuck until it lasts

        // learning and recovery timers
        float m_previous_speed  = 0.0f;
        float m_decel           = 0.0f;
        float m_full_brake_time = 0.0f;
        float m_saturated_time  = 0.0f;
        float m_stuck_time      = 0.0f;
        float m_upside_time     = 0.0f;
        float m_wrong_way_time  = 0.0f;
        float m_rebuild_time    = 0.0f;
        float m_log_time        = 0.0f;
        float m_plan_key        = 0.0f; // car model the plan was last built with
        float m_lap_error_max   = 0.0f;
        bool m_excursion        = false;
    };
}
