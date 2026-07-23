/*
Copyright(c) 2015-2026 Panos Karabelas
*/
#pragma once

#include <memory>
#include <string>

class Engine;
class PistonEngineSimulator;
class Transmission;
class Vehicle;

namespace spartan
{
    class CarEngineSimFerrari412
    {
    public:
        CarEngineSimFerrari412();
        ~CarEngineSimFerrari412();

        CarEngineSimFerrari412(
            const CarEngineSimFerrari412&
        ) = delete;
        CarEngineSimFerrari412& operator=(
            const CarEngineSimFerrari412&
        ) = delete;

        static std::unique_ptr<CarEngineSimFerrari412> create(
            const std::string& impulse_response_path =
                "project/music/"
                "engine_minimal_muffling_01.wav"
        );

        void initialize(
            const std::string& impulse_response_path =
                "project/music/"
                "engine_minimal_muffling_01.wav"
        );
        void destroy();

        ::Engine* get_engine() const;
        Vehicle* get_vehicle() const;
        Transmission* get_transmission() const;
        PistonEngineSimulator* get_simulator() const;

    private:
        class implementation;
        std::unique_ptr<implementation> m_implementation;
    };
}
