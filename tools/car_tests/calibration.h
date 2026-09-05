#pragma once
#include <fstream>
#include <sstream>

// A fitting front end calls the production brush implementation, avoiding a
// second tire model that could silently diverge from the simulator.
int evaluate_calibration(int argc, char** argv)
{
    check(argc == 7, "--tire-evaluate input.csv output.csv stiffness_long stiffness_lat mu");
    auto* definition = car::load_car_file("worlds/cars/ferrari_laferrari.car");
    check(definition != nullptr, "calibration preset");
    auto spec = definition->performance;
    spec.tread_stiffness_long = std::stof(argv[4]); spec.tread_stiffness_lat = std::stof(argv[5]); spec.tire_friction = std::stof(argv[6]);
    std::ifstream input(argv[2]); std::ofstream output(argv[3]);
    check(input.good() && output.good(), "calibration streams");
    std::string line; std::getline(input, line);
    check(line == "kappa,alpha_rad,camber_rad,fz_n,radius_m,width_m", "calibration input schema");
    output << "fx_n,fy_n\n";
    while (std::getline(input, line))
    {
        for (char& c : line) if (c == ',') c = ' ';
        std::istringstream row(line); float kappa, alpha, camber, fz, radius, width;
        check(static_cast<bool>(row >> kappa >> alpha >> camber >> fz >> radius >> width), "calibration numeric row");
        check(std::isfinite(kappa) && std::isfinite(alpha) && std::isfinite(camber) && fz > 0 && radius > 0 && width > 0, "calibration physical row");
        float peak = spec.tire_friction * spec.load_reference * powf(fz / spec.load_reference, spec.load_sensitivity);
        float saturation; auto params = car::evaluate_brush_params(spec, radius, width, fz, 1);
        auto force = car::evaluate_brush_model(spec, params, kappa, alpha, camber, fz, peak, peak, -1, saturation, 1);
        output << force.longitudinal << ',' << force.lateral << '\n';
    }
    return 0;
}
