/*
Copyright(c) 2015-2026 Panos Karabelas
Licensed under the Spartan Engine License. See license.md in the repository root.
https://github.com/PanosK92/SpartanEngine/blob/master/license.md
Commercial use requires written permission and negotiated payment terms.
*/

//= INCLUDES ==============================
#include "pch.h"
#include "WorldEnvironment.h"
#include "WorldViewer.h"
#include "../EditorHistory.h"
#include "../Editor.h"
#include "../imgui/ImGui_EditorUi.h"
#include "../imgui/ImGui_Extension.h"
#include "../imgui/ImGui_Properties.h"
#include "../imgui/source/imgui_internal.h"
#include "core/Engine.h"
#include "world/World.h"
#include "world/Entity.h"
#include "world/Environment.h"
#include "world/Weather.h"
#include "world/components/Light.h"
//=========================================

//= NAMESPACES ===============
using namespace std;
using namespace spartan;
using namespace spartan::math;
using namespace editor_ui;
//============================

namespace
{
    // the sun's real path over the current utc day at the world location: the curve is its altitude,
    // the bands are night, twilight, golden hour and day, and dragging across it scrubs the time
    void sun_arc(Light* light, const bool locked, const bool real_time)
    {
        struct Curve
        {
            double day_start = -1e20;
            double latitude  = 0.0;
            double longitude = 0.0;
            double elevation = 0.0;
            float altitude[97] = {};
        };
        static Curve curve;

        const EnvironmentSettings settings = Environment::GetSettings();
        const double days      = Environment::GetDays(real_time);
        const double day_start = floor(days + 0.5) - 0.5;
        if (curve.day_start != day_start || curve.latitude != settings.latitude || curve.longitude != settings.longitude || curve.elevation != settings.elevation)
        {
            curve.day_start = day_start;
            curve.latitude  = settings.latitude;
            curve.longitude = settings.longitude;
            curve.elevation = settings.elevation;
            for (int i = 0; i <= 96; i++)
            {
                curve.altitude[i] = Environment::GetSunAltitude(day_start + i / 96.0);
            }
        }

        bool hovered = false;
        bool held    = false;
        ImVec2 min, max;
        ImDrawList* draw_list = canvas("##sun_arc", ImGui::EditorUi::scaled(118.0f), &min, &max, &hovered, &held);

        const float pad      = ImGui::EditorUi::scaled(10.0f);
        const float label_h  = ImGui::GetTextLineHeight();
        const float x0       = min.x + pad;
        const float x1       = max.x - pad;
        const float top      = min.y + pad + label_h;
        const float bottom   = max.y - pad - label_h;
        float peak           = 10.0f;
        for (const float a : curve.altitude)
        {
            peak = ImMax(peak, fabsf(a));
        }
        const float horizon  = top + (bottom - top) * 0.62f;
        const float scale    = (horizon - top) / peak;
        auto to_x = [&](const float t) { return x0 + (x1 - x0) * t; };
        auto to_y = [&](const float a) { return ImClamp(horizon - a * scale, top, bottom); };

        // sky bands, so the length of the day reads before any number does
        for (int i = 0; i < 96; i++)
        {
            const float a = (curve.altitude[i] + curve.altitude[i + 1]) * 0.5f;
            ImVec4 band   = ImVec4(0, 0, 0, 0);
            if (a > 6.0f)
            {
                band = ImVec4(0.35f, 0.60f, 1.00f, 0.10f);
            }
            else if (a > 0.0f)
            {
                band = ImVec4(1.00f, 0.62f, 0.25f, 0.14f);
            }
            else if (a > -6.0f)
            {
                band = ImVec4(0.55f, 0.40f, 0.85f, 0.10f);
            }
            if (band.w > 0.0f)
            {
                draw_list->AddRectFilled(ImVec2(to_x(i / 96.0f), top), ImVec2(to_x((i + 1) / 96.0f), bottom), ImGui::EditorUi::color(band));
            }
        }

        draw_list->AddLine(ImVec2(x0, horizon), ImVec2(x1, horizon), ImGui::EditorUi::color(ImGui::Style::color_border_strong), 1.0f);

        const ImVec4 sun_tint = design::accent_light();
        for (int i = 0; i < 96; i++)
        {
            const bool above = curve.altitude[i] > 0.0f || curve.altitude[i + 1] > 0.0f;
            draw_list->AddLine(
                ImVec2(to_x(i / 96.0f), to_y(curve.altitude[i])),
                ImVec2(to_x((i + 1) / 96.0f), to_y(curve.altitude[i + 1])),
                ImGui::EditorUi::color(above ? sun_tint : ImGui::EditorUi::alpha(ImGui::Style::color_text_muted, 0.6f)),
                ImGui::EditorUi::scaled(above ? 2.0f : 1.25f)
            );
        }

        // hour labels
        const char* hours[] = { "00", "06", "12", "18", "24" };
        for (int i = 0; i < 5; i++)
        {
            const ImVec2 size = ImGui::CalcTextSize(hours[i]);
            const float x     = ImClamp(to_x(i / 4.0f) - size.x * 0.5f, min.x + ImGui::EditorUi::scaled(4.0f), max.x - size.x - ImGui::EditorUi::scaled(4.0f));
            draw_list->AddText(ImVec2(x, bottom + ImGui::EditorUi::scaled(2.0f)), ImGui::EditorUi::color(ImGui::Style::color_text_faint), hours[i]);
        }

        // sunrise and sunset from the horizon crossings
        float sunrise = -1.0f;
        float sunset  = -1.0f;
        for (int i = 0; i < 96; i++)
        {
            const float a = curve.altitude[i];
            const float b = curve.altitude[i + 1];
            if ((a <= 0.0f) != (b <= 0.0f))
            {
                const float t = (i + a / (a - b)) / 96.0f;
                if (b > a && sunrise < 0.0f)
                {
                    sunrise = t;
                }
                else if (b < a && sunset < 0.0f)
                {
                    sunset = t;
                }
            }
        }

        auto clock = [](const float t, char* buffer, const size_t size)
        {
            const int minutes = static_cast<int>(t * 1440.0f + 0.5f) % 1440;
            snprintf(buffer, size, "%02d:%02d", minutes / 60, minutes % 60);
        };

        char text[96];
        char a[16];
        char b[16];
        if (sunrise >= 0.0f && sunset >= 0.0f)
        {
            clock(sunrise, a, sizeof(a));
            clock(sunset, b, sizeof(b));
            snprintf(text, sizeof(text), "sunrise %s   sunset %s UTC", a, b);
        }
        else
        {
            snprintf(text, sizeof(text), curve.altitude[48] > 0.0f ? "polar day, the sun never sets" : "polar night, the sun never rises");
        }
        draw_list->AddText(ImVec2(x0, min.y + ImGui::EditorUi::scaled(4.0f)), ImGui::EditorUi::color(ImGui::Style::color_text_muted), text);

        // the sun at the current time
        const float time_of_day = World::GetTimeOfDay(real_time);
        const float altitude    = Environment::GetSunAltitude(days);
        const ImVec2 sun        = ImVec2(to_x(time_of_day), to_y(altitude));
        const bool is_up        = altitude > 0.0f;
        draw_list->AddLine(ImVec2(sun.x, top), ImVec2(sun.x, bottom), ImGui::EditorUi::color(ImGui::EditorUi::alpha(ImGui::Style::color_text, 0.18f)), 1.0f);
        if (is_up)
        {
            ImGui::EditorUi::draw_glow(draw_list, ImVec2(sun.x - 3.0f, sun.y - 3.0f), ImVec2(sun.x + 3.0f, sun.y + 3.0f), sun_tint, 3.0f, ImGui::EditorUi::scaled(10.0f), 1.0f);
        }
        draw_list->AddCircleFilled(sun, ImGui::EditorUi::scaled(5.0f), ImGui::EditorUi::color(is_up ? ImVec4(1.0f, 0.95f, 0.80f, 1.0f) : ImGui::Style::color_text_muted), 20);

        // rounded first, a sun a hair under the horizon would otherwise read as -0
        clock(time_of_day, a, sizeof(a));
        snprintf(text, sizeof(text), "%s  %+.0f\xC2\xB0", a, roundf(altitude) + 0.0f);
        const ImVec2 size = ImGui::CalcTextSize(text);
        draw_list->AddText(ImVec2(max.x - pad - size.x, min.y + ImGui::EditorUi::scaled(4.0f)), ImGui::EditorUi::color(ImGui::Style::color_text), text);

        // dragging scrubs the clock, the ghost line previews where a click would land
        if (!locked && (hovered || held))
        {
            const float t = ImClamp((ImGui::GetIO().MousePos.x - x0) / (x1 - x0), 0.0f, 0.999f);
            if (!held)
            {
                draw_list->AddLine(ImVec2(to_x(t), top), ImVec2(to_x(t), bottom), ImGui::EditorUi::color(ImGui::EditorUi::alpha(ImGui::Style::color_text, 0.35f)), 1.0f);
                clock(t, a, sizeof(a));
                ImGui::SetTooltip("%s UTC, click or drag to set the time", a);
            }
            else
            {
                light->SetTimeOfDay(t);
            }
        }
        else if (locked && hovered)
        {
            ImGui::SetTooltip("following the real clock, turn off Real time to scrub");
        }
    }

    const char* compass_name(float degrees)
    {
        static const char* names[] = { "N", "NE", "E", "SE", "S", "SW", "W", "NW" };
        degrees = fmodf(fmodf(degrees, 360.0f) + 360.0f, 360.0f);
        return names[static_cast<int>((degrees + 22.5f) / 45.0f) % 8];
    }

    // the name a weather report would use, it says what a number of m/s feels like
    const char* beaufort_name(const float speed)
    {
        if (speed < 0.5f)  return "calm";
        if (speed < 1.6f)  return "light air";
        if (speed < 3.4f)  return "light breeze";
        if (speed < 5.5f)  return "gentle breeze";
        if (speed < 8.0f)  return "moderate breeze";
        if (speed < 10.8f) return "fresh breeze";
        if (speed < 13.9f) return "strong breeze";
        if (speed < 17.2f) return "near gale";
        if (speed < 20.8f) return "gale";
        if (speed < 24.5f) return "strong gale";
        if (speed < 28.5f) return "storm";
        if (speed < 32.7f) return "violent storm";
        return "hurricane force";
    }

    // wind is stored as a world velocity, people think of it as a speed and the compass point it comes from
    void wind_rows(const float north_degrees)
    {
        static float remembered_from = 270.0f;

        Vector3 wind       = World::GetWind();
        float speed        = sqrtf(wind.x * wind.x + wind.z * wind.z);
        const float toward = atan2f(wind.x, wind.z) * rad_to_deg + north_degrees;
        float from         = speed > 0.01f ? fmodf(toward + 180.0f + 360.0f, 360.0f) : remembered_from;

        char speed_format[64];
        snprintf(speed_format, sizeof(speed_format), "%%.1f m/s \xC2\xB7 %s", beaufort_name(speed));
        const bool speed_changed = property_slider("Wind", &speed, 0.0f, 40.0f, speed_format, "shared by vegetation, clouds, rain, particles and vehicle aerodynamics");

        char from_format[64];
        snprintf(from_format, sizeof(from_format), "%%.0f\xC2\xB0 from %s", compass_name(from));
        ImGui::BeginDisabled(speed <= 0.01f);
        const bool from_changed = property_slider("Direction", &from, 0.0f, 359.0f, from_format, "the compass point the wind blows from, as a weather report gives it");
        ImGui::EndDisabled();

        if (speed_changed || from_changed)
        {
            remembered_from   = from;
            const float angle = (from + 180.0f - north_degrees) * deg_to_rad;
            World::SetWind(Vector3(speed * sinf(angle), wind.y, speed * cosf(angle)));
        }
    }
}

WorldEnvironment::WorldEnvironment(Editor* editor) : Widget(editor)
{
    m_title          = "Environment";
    m_dock           = WidgetDock::Right;
    m_size_initial.x = 460.0f;
    // joining the world panel must not pull its tab away from the hierarchy, Focus() is the explicit way in
    m_flags         |= ImGuiWindowFlags_NoFocusOnAppearing;
}

void WorldEnvironment::OnPreBegin()
{
    // a layout saved before this panel existed has no place for it, so it joins the world panel instead of floating,
    // decided once at startup and retried until the world panel is docked, it is not during the first loading frames
    if (!m_dock_checked)
    {
        m_dock_checked = true;
        m_dock_pending = ImGui::FindWindowSettingsByID(ImHashStr(m_title)) == nullptr;
        m_joins_world  = m_dock_pending;
    }

    // the base centers a new window with a first use position, and a position request undocks it in the same frame
    if (!m_joins_world)
    {
        Widget::OnPreBegin();
    }

    if (m_dock_pending)
    {
        ImGuiWindow* world = ImGui::FindWindowByName("World");
        if (world && world->DockId != 0)
        {
            ImGui::SetNextWindowDockID(world->DockId, ImGuiCond_Always);
            m_dock_pending = false;
        }
    }

    // before Begin, so it also works while the panel is a hidden tab
    if (m_focus_pending)
    {
        ImGui::SetNextWindowFocus();
        m_focus_pending = false;
    }
}

void WorldEnvironment::OnVisible()
{
    // reopened from the toolbar it should come to the front, the first appearance at startup should not
    m_focus_pending = m_appeared_once;
    m_appeared_once = true;
}

void WorldEnvironment::Focus()
{
    m_visible       = true;
    m_focus_pending = true;
}

void WorldEnvironment::OnTickVisible()
{
    const bool is_in_game_mode = Engine::IsFlagSet(EngineMode::Playing);
    if (is_in_game_mode)
    {
        ImGui::TextColored(ImGui::Style::color_warning, "Read-only during playback");
        ImGui::TextDisabled("Stop playback to change the sky and weather.");
        ImGui::Separator();
    }

    Light* sun = World::GetDirectionalLight();
    unique_ptr<editor_history::EntityScope> history;
    if (sun)
    {
        // the sun's entity snapshot carries the environment settings too, so every edit here is one undo step
        history = make_unique<editor_history::EntityScope>(sun->GetEntity(), true);
    }

    ImGui::BeginDisabled(is_in_game_mode);

    char summary[96];
    EnvironmentSettings environment_settings = Environment::GetSettings();

    if (!sun)
    {
        // clouds, rain and the clock are carried by the sun, without one only the place and climate mean anything
        ImGui::Dummy(ImVec2(0, ImGui::EditorUi::scaled(8.0f)));
        ImGui::EditorUi::panel_header("This world has no sun", "The time of day, clouds and rain are carried by a directional light. Place, wind and climate below still apply.", Editor::font_bold);
        ImGui::Dummy(ImVec2(0, ImGui::EditorUi::scaled(4.0f)));
        ImGui::EditorUi::push_primary_button();
        if (ImGui::Button("Add a sun"))
        {
            WorldViewer::ActionEntityCreateLightDirectional();
        }
        ImGui::EditorUi::pop_primary_button();
        layout::group_spacing();
    }

    bool day_night_cycle = sun && sun->GetFlag(LightFlags::DayNightCycle);
    bool real_time_cycle = sun && sun->GetFlag(LightFlags::RealTimeCycle);
    const bool real_time = day_night_cycle && real_time_cycle;

    if (sun)
    {
        // the sun's path is the time control, you see the length of the day and where noon is before touching it
        sun_arc(sun, real_time, real_time);

        // the presets are moments of that same day, so they sit right under it
        static vector<string> sky_presets = { "Dawn", "Noon", "Dusk", "Night", "Lynch" };
        uint32_t preset_index = static_cast<uint32_t>(sun->GetPreset());
        ImGui::BeginDisabled(real_time);
        if (segmented_control("##sky_preset", sky_presets, &preset_index))
        {
            sun->SetPreset(static_cast<LightPreset>(preset_index));
        }
        ImGui::EndDisabled();
        ImGuiSp::tooltip("jumps the sun to that moment of the current date, Lynch is a dreamy low sun");

        layout::group_spacing();
    }

    // weather, the thing changed most often after the time, so it comes first and starts open
    {
        float cloud_coverage = sun ? sun->GetCloudCoverage() : 0.0f;
        float rain           = sun ? sun->GetRain() : 0.0f;
        float puddliness     = World::GetPuddliness();
        const float wind     = sqrtf(World::GetWind().x * World::GetWind().x + World::GetWind().z * World::GetWind().z);
        if (rain > 0.01f)
        {
            snprintf(summary, sizeof(summary), "clouds %.0f%% \xC2\xB7 rain %.0f%% \xC2\xB7 %.0f m/s", cloud_coverage * 100.0f, rain * 100.0f, wind);
        }
        else
        {
            snprintf(summary, sizeof(summary), "clouds %.0f%% \xC2\xB7 dry \xC2\xB7 %.0f m/s", cloud_coverage * 100.0f, wind);
        }
        if (layout::fold("Weather", true, summary))
        {
            ImGui::BeginDisabled(!sun);
            if (property_percent("Clouds", &cloud_coverage, "0 is a clear sky, 100 is overcast") && sun)
            {
                sun->SetCloudCoverage(cloud_coverage);
            }
            if (property_percent("Rain", &rain, "clouds close in, surfaces soak and puddles fill over a minute of steady rain") && sun)
            {
                sun->SetRain(rain);
            }
            ImGui::EndDisabled();
            if (property_percent("Puddles", &puddliness, "standing water on terrain and roads, pools grow out of the low spots first"))
            {
                World::SetPuddliness(puddliness);
            }

            wind_rows(environment_settings.north_degrees);

            // what the simulation made of it, read only and at a glance
            const auto& conditions = World::GetEnvironment();
            char air[32], road[32], wet[32], moon[32];
            snprintf(air, sizeof(air), "%.1f \xC2\xB0""C", conditions.air_temperature);
            snprintf(road, sizeof(road), "%.1f \xC2\xB0""C", conditions.road_temperature);
            snprintf(wet, sizeof(wet), "%.0f%%", Weather::GetWetness() * 100.0f);
            snprintf(moon, sizeof(moon), "%.0f%%", conditions.moon_fraction * 100.0f);
            layout::group_spacing();
            stat_strip("##weather_stats", { { air, "air" }, { road, "road" }, { wet, "wetness" }, { moon, "moon lit" } });
        }
    }

    // time, how the clock moves rather than what it reads
    if (sun)
    {
        if (!day_night_cycle)
        {
            snprintf(summary, sizeof(summary), "frozen");
        }
        else if (real_time_cycle)
        {
            snprintf(summary, sizeof(summary), "real clock");
        }
        else
        {
            snprintf(summary, sizeof(summary), "%.0f\xC3\x97 speed", environment_settings.time_scale);
        }
        if (layout::fold("Time", true, summary))
        {
            if (property_toggle("Animate", &day_night_cycle, "move the sun on its own, otherwise it stays where you put it"))
            {
                sun->SetFlag(LightFlags::DayNightCycle, day_night_cycle);
            }

            ImGui::BeginDisabled(!day_night_cycle);
            if (property_toggle("Real time", &real_time_cycle, "follow the computer's clock instead of the simulated one"))
            {
                sun->SetFlag(LightFlags::RealTimeCycle, real_time_cycle);
            }

            ImGui::BeginDisabled(real_time_cycle);
            EnvironmentSettings settings = Environment::GetSettings();
            float rate = static_cast<float>(settings.time_scale);
            if (property_slider("Clock speed", &rate, 0.0f, 86400.0f, "%.0f\xC3\x97", "simulated seconds per real second, 1 is real speed, 60 turns a minute into an hour", ImGuiSliderFlags_Logarithmic))
            {
                settings.time_scale = rate;
                Environment::SetSettings(settings);
            }
            ImGui::EndDisabled();
            ImGui::EndDisabled();
        }
    }

    // place and date, the sun's path depends on both
    {
        environment_settings = Environment::GetSettings();
        int year, month, day, hour, minute; double second;
        Environment::GetDate(year, month, day, hour, minute, second, real_time);
        snprintf(
            summary,
            sizeof(summary),
            "%.2f\xC2\xB0 %c  %.2f\xC2\xB0 %c \xC2\xB7 %04d-%02d-%02d",
            fabs(environment_settings.latitude), environment_settings.latitude >= 0.0 ? 'N' : 'S',
            fabs(environment_settings.longitude), environment_settings.longitude >= 0.0 ? 'E' : 'W',
            year, month, day
        );
        if (layout::fold("Place and date", false, summary))
        {
            bool environment_changed = false;
            int date[3] = { year, month, day };
            layout::begin_property("Date (UTC)", "year, month, day from 1800 to 2200, press Enter to apply");
            ImGui::BeginDisabled(real_time);
            if (ImGui::InputInt3("##earth_date", date, ImGuiInputTextFlags_EnterReturnsTrue))
            {
                if (Environment::SetDate(date[0], date[1], date[2], hour, minute, second) && sun)
                {
                    sun->SetTimeOfDay(World::GetTimeOfDay());
                }
                environment_settings = Environment::GetSettings();
            }
            ImGui::EditorUi::decorate_field();
            ImGui::EndDisabled();

            float latitude  = static_cast<float>(environment_settings.latitude);
            float longitude = static_cast<float>(environment_settings.longitude);
            float elevation = static_cast<float>(environment_settings.elevation);
            environment_changed |= property_slider("Latitude", &latitude, -90.0f, 90.0f, "%.2f\xC2\xB0", "degrees north, negative is south, it sets how high the sun climbs and how long the day is");
            environment_changed |= property_slider("Longitude", &longitude, -180.0f, 180.0f, "%.2f\xC2\xB0", "degrees east, negative is west, it shifts when local noon happens in utc");
            environment_changed |= property_float("Elevation", &elevation, 1.0f, -400.0f, 10000.0f, "meters above sea level, thins the air and cools it", "%.0f m");
            environment_changed |= property_slider("North", &environment_settings.north_degrees, -180.0f, 180.0f, "%.0f\xC2\xB0", "compass heading of world +Z, 0 means +Z is north and +X is east");
            if (environment_changed)
            {
                environment_settings.latitude  = latitude;
                environment_settings.longitude = longitude;
                environment_settings.elevation = elevation;
                Environment::SetSettings(environment_settings);
                if (sun && !day_night_cycle)
                {
                    sun->SetTimeOfDay(World::GetTimeOfDay());
                }
            }
        }
    }

    // climate is set once per location, so it stays folded
    {
        snprintf(summary, sizeof(summary), "%.1f \xC2\xB0""C mean", environment_settings.annual_temperature);
        if (layout::fold("Climate", false, summary))
        {
            bool climate_changed = false;
            climate_changed |= property_slider("Annual mean", &environment_settings.annual_temperature, -70.0f, 50.0f, "%.1f \xC2\xB0""C", "sea level average temperature over a year, a configurable climate rather than recorded weather");
            climate_changed |= property_slider("Seasonal swing", &environment_settings.seasonal_amplitude, 0.0f, 40.0f, "\xC2\xB1%.1f \xC2\xB0""C", "how far summer and winter move either side of the mean");
            climate_changed |= property_slider("Daily swing", &environment_settings.daily_amplitude, 0.0f, 20.0f, "\xC2\xB1%.1f \xC2\xB0""C", "how far a clear afternoon and night move either side of the day's average");
            float pressure_hpa = environment_settings.sea_level_pressure / 100.0f;
            if (property_slider("Pressure", &pressure_hpa, 870.0f, 1085.0f, "%.0f hPa", "sea level pressure, altitude adjusts the local air density and tire gauge pressure"))
            {
                environment_settings.sea_level_pressure = pressure_hpa * 100.0f;
                climate_changed = true;
            }
            if (climate_changed)
            {
                Environment::SetSettings(environment_settings);
            }

            const auto& conditions = World::GetEnvironment();
            char density[64];
            snprintf(density, sizeof(density), "Air density here is %.3f kg/m\xC2\xB3 at %.0f hPa.", conditions.air_density, conditions.pressure / 100.0f);
            layout::note(density, ImGui::Style::color_text_muted);
        }
    }

    ImGui::EndDisabled();
}
