#ifndef ATG_ENGINE_SIM_UNITS_H
#define ATG_ENGINE_SIM_UNITS_H

#include "constants.h"

namespace units {
    // Force
    inline constexpr double N = 1.0;

    inline constexpr double lbf = N * 4.44822;

    // Mass
    inline constexpr double kg = 1.0;
    inline constexpr double g = kg / 1000.0;

    inline constexpr double lb = 0.45359237 * kg;

    // Distance
    inline constexpr double m = 1.0;
    inline constexpr double cm = m / 100.0;
    inline constexpr double mm = m / 1000.0;
    inline constexpr double km = m * 1000.0;

    inline constexpr double inch = cm * 2.54;
    inline constexpr double foot = inch * 12.0;
    inline constexpr double thou = inch / 1000.0;
    inline constexpr double mile = m * 1609.344;

    // Time
    inline constexpr double sec = 1.0;
    inline constexpr double minute = 60 * sec;
    inline constexpr double hour = 60 * minute;

    // Torque
    inline constexpr double Nm = N * m;
    inline constexpr double ft_lb = foot * lbf;

    // Power
    inline constexpr double W = Nm / sec;
    inline constexpr double kW = W * 1000.0;
    inline constexpr double hp = 745.699872 * W;

    // Volume
    inline constexpr double m3 = 1.0;
    inline constexpr double cc = cm * cm * cm;
    inline constexpr double mL = cc;
    inline constexpr double L = mL * 1000.0;
    inline constexpr double cubic_feet = foot * foot * foot;
    inline constexpr double cubic_inches = inch * inch * inch;
    inline constexpr double gal = 3.785411784 * L;

    // Molecular
    inline constexpr double mol = 1.0;
    inline constexpr double kmol = mol / 1000.0;
    inline constexpr double mmol = mol / 1000000.0;
    inline constexpr double lbmol = mol * 453.59237;

    // Flow-rate (moles)
    inline constexpr double mol_per_sec = mol / sec;
    inline constexpr double scfm = 0.002641 * lbmol / minute;

    // Area
    inline constexpr double m2 = 1.0;
    inline constexpr double cm2 = cm * cm;

    // Pressure
    inline constexpr double Pa = 1.0;
    inline constexpr double kPa = Pa * 1000.0;
    inline constexpr double MPa = Pa * 1000000.0;
    inline constexpr double atm = 101.325 * kPa;

    inline constexpr double mbar = Pa * 100.0;
    inline constexpr double bar = mbar * 1000.0;

    inline constexpr double psi = lbf / (inch * inch);
    inline constexpr double psig = psi;
    inline constexpr double inHg = Pa * 3386.3886666666713;
    inline constexpr double inH2O = inHg * 0.0734824;

    // Temperature
    inline constexpr double K = 1.0;
    inline constexpr double K0 = 273.15;
    inline constexpr double C = K;
    inline constexpr double F = (5.0 / 9.0) * K;
    inline constexpr double F0 = -459.67;

    // Energy
    inline constexpr double J = 1.0;
    inline constexpr double kJ = J * 1000;
    inline constexpr double MJ = J * 1000000;

    // Angles
    inline constexpr double rad = 1.0;
    inline constexpr double deg = rad * (constants::pi / 180);

    // Conversions
    inline constexpr double distance(double v, double unit) {
        return v * unit;
    }

    inline constexpr double area(double v, double unit) {
        return v * unit;
    }

    inline constexpr double torque(double v, double unit) {
        return v * unit;
    }

    inline constexpr double rpm(double rpm) {
        return rpm * 0.104719755;
    }

    inline constexpr double toRpm(double rad_s) {
        return rad_s / 0.104719755;
    }

    inline constexpr double pressure(double v, double unit) {
        return v * unit;
    }

    inline constexpr double psia(double p) {
        return units::pressure(p, units::psig) - units::pressure(1.0, units::atm);
    }

    inline constexpr double toPsia(double p) {
        return (p + units::pressure(1.0, units::atm)) / units::psig;
    }

    inline constexpr double mass(double v, double unit) {
        return v * unit;
    }

    inline constexpr double force(double v, double unit) {
        return v * unit;
    }

    inline constexpr double volume(double v, double unit) {
        return v * unit;
    }

    inline constexpr double flow(double v, double unit) {
        return v * unit;
    }

    inline constexpr double convert(double v, double unit0, double unit1) {
        return v * (unit0 / unit1);
    }

    inline constexpr double convert(double v, double unit) {
        return v / unit;
    }

    inline constexpr double celcius(double T_C) {
        return T_C * C + K0;
    }

    inline constexpr double kelvin(double T) {
        return T * K;
    }

    inline constexpr double fahrenheit(double T_F) {
        return F * (T_F - F0);
    }

    inline constexpr double toAbsoluteFahrenheit(double T) {
        return T / F;
    }

    inline constexpr double angle(double v, double unit) {
        return v * unit;
    }

    inline constexpr double energy(double v, double unit) {
        return v * unit;
    }

    // Physical Constants
    constexpr double AirMolecularMass = units::mass(28.97, units::g) / units::mol;
};

#endif /* ATG_ENGINE_SIM_UNITS_H */
