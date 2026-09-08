#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdio>
#include <stdexcept>
#include "../../binaries/lighting_tests/cpu_functions.h"

void near(double actual,double expected,double tolerance,const char* label)
{
    if (!std::isfinite(actual) || std::abs(actual-expected)>tolerance) {
        std::printf("FAIL %s: %.9g != %.9g\n",label,actual,expected);
        throw std::runtime_error(label);
    }
}
int main()
{
    TestLight light{LightType::Point};
    near(light.intensity()*683*4*pi,800,0.001,"point total lumens");
    light.m_light_type=LightType::Area;
    near(light.intensity()*683*pi*6,800,0.001,"area hemispherical flux");
    light.m_light_type=LightType::Directional;
    near(light.intensity()*683,800,0.001,"directional lux round trip");
    light.m_light_type=LightType::Spot;
    double worst=0;
    for (double degrees : {0.6,1.,5.,15.,30.,60.,89.}) {
        light.m_angle_rad=float(degrees*pi/180);
        // Independent double-precision quadrature in theta, including sin(theta).
        double angle=light.m_angle_rad, integral=0;
        const int n=100000;
        for(int i=0;i<n;i++) {
            double theta=angle*(i+0.5)/n;
            double a=std::clamp((std::cos(theta)-std::cos(angle))/(std::cos(angle*.9)-std::cos(angle)),0.,1.);
            integral+=a*a*std::sin(theta)*angle/n*2*3.141592653589793;
        }
        double flux=light.intensity()*683*integral;
        worst=std::max(worst,std::abs(flux/800-1));
        near(flux,800,1.0,"spot integrated lumens");
    }
    TestCamera camera;
    camera.m_aperture=16;camera.m_shutter_speed=1.0f/125;camera.m_iso=100;
    near(camera.exposure(),1.0/(1.2*32000),1e-10,"sunny EV camera");
    float base=camera.exposure();camera.m_iso*=2;
    near(camera.exposure()/base,2,1e-6,"ISO stop");
    camera.m_shutter_speed*=2;
    near(camera.exposure()/base,4,1e-6,"shutter stop");
    camera.m_aperture*=std::sqrt(2.0f);
    near(camera.exposure()/base,2,1e-6,"aperture stop");
    for(float t : {1000.f,1850.f,2700.f,4000.f,6500.f,15000.f,40000.f}) {
        float r,g,b;temperature_to_color(t,r,g,b);
        near(.2126*r+.7152*g+.0722*b,1,1e-6,"temperature unit luminance");
    }
    std::printf("PASS point/area/directional units; integrated spot flux worst error %.5f%%; camera stops; temperature luminance\n",worst*100);
}
