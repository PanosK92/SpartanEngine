/*
Copyright(c) 2015-2026 Panos Karabelas
Licensed under the Spartan Engine License. See license.md in the repository root.
https://github.com/PanosK92/SpartanEngine/blob/master/license.md
Commercial use requires written permission and negotiated payment terms.
*/
#pragma once
#include "CarPhysicsMeshView.h"

namespace spartan
{
    // Scalar models have no housing transforms or CAD geometry. Their cutaway
    // packaging comes from the per-car catalogue or XML; estimated mounts are labelled.
    // Dimensions supplied by the preset and every
    // animated degree of freedom below come from the simulation. These entities
    // are render-only and must never add mass, collision or forces to the solver.
    inline void CarPhysicsMeshView::draw_systems(Entity* root, Entity* vehicle, CarPhysics& physics, ::car::Simulation& sim)
    {
        const auto& p = sim.get_spec();
        const auto& cfg = sim.get_config();
        const auto& layout = p.packaging;
        auto vec = [](const float* v) { return Vector3(v[0], v[1], v[2]); };
        auto frame = [&](const ::car::component_packaging& c) { return Quaternion::FromRotation(Vector3::Forward, vec(c.axis)); };
        auto mounted = [&](const ::car::component_packaging& c, const Vector3& offset) { return vec(c.position) + frame(c) * offset; };
        const auto& phases = sim.get_visual_rotations();
        const auto& mb = sim.get_multibody_state();
        const Quaternion chassis_rotation = vehicle->GetRotation();
        const Vector3 chassis_position = vehicle->GetPosition();
        auto world = [&](const Vector3& v) { return chassis_position + chassis_rotation * v; };
        auto local_actor = [&](const physx::PxVec3& v)
        {
            return chassis_rotation.Conjugate() * (physics.TransformVehiclePointToRender(PhysicsWorld::ToWorldPosition(vector(v))) - chassis_position);
        };
        auto wheel_center = [&](int i)
        {
            if (mb.corners[i].wheel_body)
                return local_actor(mb.corners[i].wheel_body->getGlobalPose().p);
            if (Entity* wheel = physics.GetWheelEntity(static_cast<WheelIndex>(i)))
                return chassis_rotation.Conjugate() * (wheel->GetPosition() - chassis_position);
            return Vector3((i % 2 == 0 ? -0.5f : 0.5f) * (i < 2 ? cfg.track_front : cfg.track_rear),
                -cfg.wheel_radius_for(i), (i < 2 ? 0.5f : -0.5f) * cfg.wheelbase);
        };
        auto spin = [](const Vector3& axis, float angle)
        {
            return Quaternion::FromRotation(Vector3::Right, axis) * Quaternion::FromAxisAngle(Vector3::Right, angle);
        };
        // Unit parts share geometry across every system and every frame. Cylinders,
        // rings and gears run along X and have length 1, radius 1; boxes have size 1.
        auto part = [&](const std::string& name, const std::string& kind, const Vector3& center,
            const Quaternion& orientation, const Vector3& size, const std::shared_ptr<Material>& finish)
        {
            draw(root, name, "system/" + kind, world(center), chassis_rotation * orientation, size, finish, [&]
            {
                if (kind == "box") return shape_mesh(physx::PxBoxGeometry(0.5f, 0.5f, 0.5f));
                Geometry g;
                if (kind == "gear")
                {
                    // Machined rim, hub bore and 32 teeth, with bevelled tooth tips.
                    constexpr int segments = 128;
                    for (int i = 0; i < segments; ++i)
                    {
                        const float a = static_cast<float>(i) * math::pi * 2 / segments;
                        const float b = static_cast<float>(i + 1) * math::pi * 2 / segments;
                        auto radius = [](int j) { return (j % 4 == 1 || j % 4 == 2) ? 1.0f : 0.88f; };
                        auto v = [](float x, float r, float angle) { return Vector3(x, r * cosf(angle), r * sinf(angle)); };
                        const float ra = radius(i), rb = radius(i + 1);
                        const float xs[] = {-0.5f,-0.38f,0.38f,0.5f};
                        const float bevel[] = {0.94f,1.0f,1.0f,0.94f};
                        for (int k = 0; k < 3; ++k)
                        {
                            const auto v0 = v(xs[k], ra * bevel[k], a), v1 = v(xs[k], rb * bevel[k], b);
                            const auto v2 = v(xs[k+1], ra * bevel[k+1], a), v3 = v(xs[k+1], rb * bevel[k+1], b);
                            g.triangle(v0,v1,v2); g.triangle(v2,v1,v3);
                        }
                        for (int side : {-1,1})
                        {
                            const float x = static_cast<float>(side) * 0.5f;
                            const auto ia = v(x,0.24f,a), ib = v(x,0.24f,b);
                            const auto oa = v(x,ra*0.94f,a), ob = v(x,rb*0.94f,b);
                            if (side > 0) { g.triangle(ia,oa,ob); g.triangle(ia,ob,ib); }
                            else { g.triangle(ia,ob,oa); g.triangle(ia,ib,ob); }
                        }
                    }
                    g.lathe({{0.5f,0.24f},{-0.5f,0.24f}}, 128);
                }
                else if (kind == "ring")
                    g.lathe({{-0.5f,0.78f},{-0.5f,0.94f},{-0.40f,1.0f},{0.40f,1.0f},{0.5f,0.94f},{0.5f,0.78f},{-0.5f,0.78f}}, 96);
                else
                    g.lathe({{-0.5f,0.0f},{-0.5f,0.92f},{-0.42f,1.0f},{0.42f,1.0f},{0.5f,0.92f},{0.5f,0.0f}}, 96);
                return g.finish();
            });
        };
        auto box = [&](const std::string& name, const Vector3& center, const Vector3& size, const std::shared_ptr<Material>& finish)
        { part(name,"box",center,Quaternion::Identity,size,finish); };
        auto link = [&](const std::string& name, const Vector3& a, const Vector3& b, float radius, const std::shared_ptr<Material>& finish, float angle = 0.0f)
        {
            const float length = (b-a).Length();
            if (length > 0.0001f) part(name,"cylinder",(a+b)*0.5f,spin((b-a)/length,angle),Vector3(length,radius,radius),finish);
        };
        auto disc = [&](const std::string& name, const std::string& kind, const Vector3& center, const Vector3& axis,
            float thickness, float radius, float angle, const std::shared_ptr<Material>& finish)
        { part(name,kind,center,spin(axis,angle),Vector3(thickness,radius,radius),finish); };

        Vector3 crank(p.engine_crank_axis_x,p.engine_crank_axis_y,p.engine_crank_axis_z);
        if (crank.LengthSquared() < 0.0001f) crank = Vector3::Right;
        crank.Normalize();
        Vector3 up = Vector3::Up - crank * Vector3::Dot(Vector3::Up,crank);
        if (up.LengthSquared() < 0.0001f) up = Vector3::Forward;
        up.Normalize();
        const Vector3 across = Vector3::Cross(crank,up).Normalized();
        const int cylinders = std::clamp(p.engine_sound_cylinders,1,::car::max_engine_cylinders);
        const int banks = std::clamp(p.engine_sound_banks,1,2);
        const float bore = p.engine_bore_mm > 0 ? p.engine_bore_mm * 0.001f : 0.086f;
        const float stroke = p.engine_stroke_mm > 0 ? p.engine_stroke_mm * 0.001f : 0.086f;
        const float pitch = bore * 1.22f;
        const int rows = (cylinders + banks - 1) / banks;
        const float engine_length = static_cast<float>(rows) * pitch;
        const Vector3 engine = vec(layout.engine.position);
        const float bank_angle = (p.engine_bank_angle_deg > 0 ? p.engine_bank_angle_deg : cylinders == 12 ? 60.0f : 90.0f) * math::deg_to_rad;
        const float crank_angle = phases.combustion_cycle;
        link("Engine / crankshaft",engine-crank*engine_length*0.6f,engine+crank*engine_length*0.6f,0.022f,chrome,crank_angle);
        // Open crankcase rails preserve visibility of the individual cylinders.
        for (int side : {-1,1})
        {
            const Vector3 offset = across * (static_cast<float>(side) * bore * 0.85f) - up*0.035f;
            link("Engine / crankcase rail " + std::to_string(side),engine+offset-crank*engine_length*0.55f,
                engine+offset+crank*engine_length*0.55f,0.025f,alloy);
        }
        int bank_rows[2] = {};
        for (int cylinder = 0; cylinder < cylinders; ++cylinder)
        {
            const int bank = banks == 1 ? 0 : std::clamp(static_cast<int>(p.engine_cylinder_bank[cylinder]),0,1);
            const float bank_rotation = banks == 1 ? 0 : (bank == 0 ? -0.5f : 0.5f) * bank_angle;
            const Vector3 direction = Quaternion::FromAxisAngle(crank,bank_rotation) * up;
            const Vector3 side = Vector3::Cross(crank,direction).Normalized();
            const Vector3 base = engine + crank * ((static_cast<float>(bank_rows[bank]++) - (rows-1)*0.5f)*pitch);
            float firing_phase = 0;
            for (int firing = 0; firing < cylinders; ++firing)
            {
                if (static_cast<int>(p.engine_firing_order[firing]) == cylinder) break;
                firing_phase += p.engine_firing_intervals_deg[firing] > 0 ? p.engine_firing_intervals_deg[firing] * math::deg_to_rad : 4*math::pi/static_cast<float>(cylinders);
            }
            const float phase = crank_angle - firing_phase;
            const float throw_radius = stroke*0.5f, rod_length = stroke*1.7f;
            const Vector3 pin = base + direction*(cosf(phase)*throw_radius) + side*(sinf(phase)*throw_radius);
            const float travel = cosf(phase)*throw_radius + sqrtf(std::max(rod_length*rod_length-throw_radius*throw_radius*sinf(phase)*sinf(phase),0.0f));
            const Vector3 piston = base + direction*travel;
            const std::string name = "Engine / cylinder " + std::to_string(cylinder+1);
            link(name+" connecting rod",pin,piston,0.009f,steel);
            disc(name+" crank web","cylinder",base+crank*0.014f,crank,0.014f,throw_radius*1.3f,phase,steel);
            disc(name+" piston","cylinder",piston,direction,bore*0.45f,bore*0.48f,0,alloy);
            for (int ring = 0; ring < 2; ++ring)
                disc(name+" piston ring "+std::to_string(ring),"ring",piston+direction*(bore*(0.13f-static_cast<float>(ring)*0.09f)),direction,0.003f,bore*0.49f,0,chrome);
            // An open liner frame, not an opaque block that hides piston travel.
            for (int rib : {-1,1})
                link(name+" liner cutaway "+std::to_string(rib),base+side*(static_cast<float>(rib)*bore*0.54f),
                    base+direction*(rod_length+stroke)+side*(static_cast<float>(rib)*bore*0.54f),0.006f,alloy);
            const Vector3 head = base + direction*(rod_length+stroke);
            disc(name+" cylinder head","ring",head,direction,0.025f,bore*0.64f,0,alloy);
            const int valves = std::clamp(p.valves_per_cylinder,2,5);
            for (int valve = 0; valve < valves; ++valve)
            {
                const bool intake = valve < valves/2;
                const float angle = static_cast<float>(valve)*2*math::pi/static_cast<float>(valves);
                const float lift = std::max(0.0f,cosf(phase*0.5f+(intake ? math::pi*0.7f : -math::pi*0.7f)))*bore*0.1f;
                const Vector3 seat = head+(crank*cosf(angle)+side*sinf(angle))*bore*0.27f-direction*lift;
                link(name+" valve stem "+std::to_string(valve),seat,seat+direction*0.045f,0.003f,chrome);
                disc(name+" valve "+std::to_string(valve),"cylinder",seat,direction,0.004f,bore*0.13f,0,intake ? alloy : brake);
            }
            link(name+" spark plug",head,head+direction*0.04f,0.006f,chrome);
            const Vector3 intake = head+side*bore*0.85f+up*0.05f;
            link(name+" intake runner",head+side*bore*0.5f,intake,0.018f,alloy);
            disc(name+" throttle plate","cylinder",intake,crank,0.003f,0.016f,sim.get_throttle()*math::pi*0.5f,steel);
            link(name+" exhaust primary",head-side*bore*0.5f,head-side*(bore*1.5f)-up*0.09f,0.018f,brake);
        }
        for (int bank = 0; bank < banks; ++bank)
        {
            const Vector3 direction = Quaternion::FromAxisAngle(crank,banks == 1 ? 0 : (bank == 0 ? -0.5f : 0.5f)*bank_angle)*up;
            for (int cam = 0; cam < std::max(p.camshafts_per_bank,1); ++cam)
            {
                const Vector3 center = engine+direction*(stroke*2.8f)+across*((static_cast<float>(cam)-0.5f)*bore*0.45f);
                const std::string name = "Engine / bank "+std::to_string(bank)+" cam "+std::to_string(cam);
                link(name,center-crank*engine_length*0.55f,center+crank*engine_length*0.55f,0.012f,chrome,crank_angle*0.5f);
                disc(name+" timing sprocket","gear",center-crank*engine_length*0.58f,crank,0.012f,0.045f,crank_angle*0.5f,steel);
            }
        }

        const Vector3 output_direction = crank * layout.engine_output_side;
        const Vector3 flywheel = engine+output_direction*(engine_length*0.6f+0.035f);
        disc("Clutch / flywheel","gear",flywheel,crank,0.025f,0.12f,crank_angle,steel);
        const Vector3 clutch_center = flywheel+output_direction*(0.035f+(1-sim.get_clutch())*0.035f);
        disc("Clutch / driven friction plate","cylinder",clutch_center,crank,0.012f,0.105f,phases.gearbox_input,brake);
        disc("Clutch / pressure plate","ring",clutch_center+output_direction*0.02f,crank,0.012f,0.12f,crank_angle,chrome);
        const Vector3 starter = flywheel-up*0.13f;
        disc("Engine / starter motor","cylinder",starter,crank,0.14f,0.045f,0,steel);
        disc("Engine / starter pinion","gear",starter+crank*0.08f,crank,0.016f,0.025f,sim.get_starter_engaged() ? crank_angle*5 : 0,chrome);

        const int gear_count = std::clamp(p.gear_count,2,::car::max_gears);
        const float gearbox_length = layout.gearbox.size[2];
        const float gear_pitch = gearbox_length * 0.80f / gear_count;
        const float shaft_spacing = layout.gearbox.size[0] * 0.44f;
        const Vector3 gearbox = vec(layout.gearbox.position);
        const Vector3 gear_axis = vec(layout.gearbox.axis);
        const Vector3 gear_right = frame(layout.gearbox) * Vector3::Right;
        const Vector3 input = gearbox-gear_right*(shaft_spacing*0.5f), output = gearbox+gear_right*(shaft_spacing*0.5f);
        link("Gearbox / input connection",clutch_center,input-gear_axis*gearbox_length*0.5f,0.025f,chrome,phases.gearbox_input);
        link("Gearbox / input shaft",input-gear_axis*gearbox_length*0.55f,input+gear_axis*gearbox_length*0.55f,0.018f,chrome,phases.gearbox_input);
        const int selected = std::clamp(sim.get_current_gear(),0,gear_count-1);
        link("Gearbox / output shaft",output-gear_axis*gearbox_length*0.55f,output+gear_axis*gearbox_length*0.55f,0.02f,chrome,phases.gearbox_gears[selected]);
        part("Gearbox / cutaway sump","box",mounted(layout.gearbox,Vector3(0,-layout.gearbox.size[1]*0.5f,0)),frame(layout.gearbox),Vector3(layout.gearbox.size[0],0.025f,gearbox_length),alloy);
        for (int gear = 0; gear < gear_count; ++gear)
        {
            const float ratio = fabsf(p.gear_ratios[gear]);
            if (ratio < 0.001f) continue;
            const float r_in = shaft_spacing/(1+ratio), r_out = shaft_spacing-r_in;
            const float z = (static_cast<float>(gear)-(gear_count-1)*0.5f)*gear_pitch;
            const auto finish = gear == selected && !sim.get_is_shifting() ? spring : alloy;
            disc("Gearbox / gear "+std::to_string(gear)+" input","gear",input+gear_axis*z,gear_axis,gear_pitch*0.4f,r_in,phases.gearbox_input,finish);
            disc("Gearbox / gear "+std::to_string(gear)+" output","gear",output+gear_axis*z,gear_axis,gear_pitch*0.4f,r_out,-phases.gearbox_gears[gear],finish);
        }
        disc("Gearbox / synchronizer selector","ring",output+gear_axis*((selected-(gear_count-1)*0.5f)*gear_pitch+0.025f),
            gear_axis,0.018f,0.035f,phases.gearbox_gears[selected],copper);

        const Vector3 shaft_start = output+gear_axis*gearbox_length*0.5f;
        const Vector3 shaft_elbow = shaft_start;
        for (int axle = 0; axle < 2; ++axle)
        {
            if ((axle == 0 && p.drivetrain_type == 0) || (axle == 1 && p.drivetrain_type == 1)) continue;
            const int left = axle*2, right = left+1;
            const Vector3 a = wheel_center(left);
            const Vector3 b = wheel_center(right);
            const Vector3 diff = (a+b)*0.5f;
            const std::string name = axle == 0 ? "Front differential" : "Rear differential";
            link(name+" / torsion driveshaft",shaft_elbow,diff,0.035f,chrome,phases.shaft);
            // A narrow witness stripe makes the scalar shaft's torsional deflection visible.
            const Vector3 axis = (diff-shaft_elbow).Normalized();
            Vector3 radial = Vector3::Cross(axis,Vector3::Up);
            if (radial.LengthSquared() < 0.0001f) radial = Vector3::Right;
            radial.Normalize();
            for (int segment = 0; segment < 12; ++segment)
            {
                const float t0 = static_cast<float>(segment)/12, t1 = static_cast<float>(segment+1)/12;
                const Vector3 r0 = Quaternion::FromAxisAngle(axis,phases.shaft+sim.get_driveshaft_twist()*t0)*radial*0.036f;
                const Vector3 r1 = Quaternion::FromAxisAngle(axis,phases.shaft+sim.get_driveshaft_twist()*t1)*radial*0.036f;
                link(name+" / shaft witness "+std::to_string(segment),shaft_elbow+(diff-shaft_elbow)*t0+r0,shaft_elbow+(diff-shaft_elbow)*t1+r1,0.002f,copper);
            }
            const float carrier = (sim.get_wheel_rotation(left)+sim.get_wheel_rotation(right))*0.5f;
            disc(name+" / crown wheel","gear",diff,Vector3::Right,0.035f,0.13f,carrier,alloy);
            disc(name+" / pinion","gear",diff+Vector3(0,-0.06f,0.10f),Vector3::Forward,0.05f,0.05f,carrier*p.final_drive,steel);
            disc(name+" / left side gear","gear",diff-Vector3(0.05f,0,0),Vector3::Right,0.025f,0.065f,sim.get_wheel_rotation(left),chrome);
            disc(name+" / right side gear","gear",diff+Vector3(0.05f,0,0),Vector3::Right,0.025f,0.065f,sim.get_wheel_rotation(right),chrome);
            if (p.diff_type != 0)
                disc(name+" / locking clutch pack","ring",diff,Vector3::Right,0.10f,0.095f,carrier,brake);
        }

        if (p.electric_enabled)
        {
            const Vector3 motor = vec(layout.motor.position);
            const Vector3 motor_axis = vec(layout.motor.axis);
            const float motor_radius = std::min(layout.motor.size[0],layout.motor.size[1])*0.5f;
            const float motor_length = layout.motor.size[2];
            disc("Electric motor / stator cutaway","ring",motor,motor_axis,motor_length,motor_radius,0,alloy);
            disc("Electric motor / rotor","cylinder",motor,motor_axis,motor_length*1.05f,motor_radius*0.62f,phases.motor,steel);
            const int poles = std::clamp(p.electric_motor_pole_pairs*2,2,32);
            for (int pole = 0; pole < poles; ++pole)
            {
                const float angle = static_cast<float>(pole)*2*math::pi/static_cast<float>(poles);
                const Vector3 offset = frame(layout.motor) * Vector3(cosf(angle)*motor_radius*0.81f,sinf(angle)*motor_radius*0.81f,0);
                disc("Electric motor / winding "+std::to_string(pole),"ring",motor+offset,motor_axis,motor_length*1.05f,motor_radius*0.17f,0,copper);
                const Vector3 magnet = Quaternion::FromAxisAngle(motor_axis,phases.motor)*offset*0.7f;
                disc("Electric motor / rotor pole "+std::to_string(pole),"cylinder",motor+magnet,motor_axis,motor_length*1.06f,motor_radius*0.11f,phases.motor,chrome);
            }
            link("Electric motor / gearbox connection",motor-motor_axis*motor_length*0.5f,shaft_start,0.028f,steel,phases.motor);
            const Vector3 battery = vec(layout.battery.position), inverter = vec(layout.inverter.position);
            const Vector3 pack_size = vec(layout.battery.size), inverter_size = vec(layout.inverter.size);
            auto package_box = [&](const std::string& name, const ::car::component_packaging& c,
                const Vector3& offset, const Vector3& size, const std::shared_ptr<Material>& finish)
            { part(name,"box",mounted(c,offset),frame(c),size,finish); };
            package_box("Battery / open enclosure",layout.battery,Vector3(0,-pack_size.y*0.5f,0),Vector3(pack_size.x,0.012f,pack_size.z),steel);
            const float soc = std::clamp(sim.get_hybrid_state().energy_j/std::max(p.battery_capacity_kwh*3600000.0f,1.0f),0.0f,1.0f);
            charge->SetPropertyRuntime(MaterialProperty::EmissiveIntensity,0.05f+soc*0.45f);
            charge->SetPropertyRuntime(MaterialProperty::EmissiveColorG,0.5f);
            const Vector3 module_size(pack_size.x/3*0.88f,pack_size.y*0.7f,pack_size.z/4*0.88f);
            for (int cell = 0; cell < 12; ++cell)
            {
                const Vector3 offset((static_cast<float>(cell%3)-1)*pack_size.x/3,0,(static_cast<float>(cell/3)-1.5f)*pack_size.z/4);
                package_box("Battery / module "+std::to_string(cell),layout.battery,offset,module_size,alloy);
                package_box("Battery / charge bar "+std::to_string(cell),layout.battery,offset+Vector3((soc-1)*module_size.x*0.4f,pack_size.y*0.4f,0),Vector3(std::max(soc*module_size.x*0.8f,0.001f),0.006f,module_size.z*0.7f),charge);
            }
            package_box("Electric motor / inverter",layout.inverter,Vector3::Zero,inverter_size,alloy);
            for (int fin = 0; fin < 9; ++fin)
                package_box("Inverter / cooling fin "+std::to_string(fin),layout.inverter,Vector3((static_cast<float>(fin)-4)*inverter_size.x/10,inverter_size.y*0.6f,0),Vector3(0.006f,inverter_size.y*0.2f,inverter_size.z),chrome);
            link("High voltage / battery to inverter",mounted(layout.battery,Vector3(pack_size.x*0.5f,0,0)),inverter,0.012f,electrical);
            for (int phase = 0; phase < 3; ++phase)
                link("High voltage / motor phase "+std::to_string(phase),mounted(layout.inverter,Vector3(0.04f*phase,0,0)),mounted(layout.motor,Vector3(0.04f*phase,0,0)),0.008f,electrical);
            const float thermal = std::clamp((sim.get_hybrid_state().temperature-p.brake_ambient_temp)/std::max(p.battery_cutoff_temp-p.brake_ambient_temp,1.0f),0.0f,1.0f);
            package_box("Battery / thermal state bar",layout.battery,Vector3(pack_size.x*0.5f+0.02f,0,0),Vector3(0.018f,std::max(pack_size.y*thermal,0.002f),0.06f),spring);
        }

        if (sim.get_turbo_enabled())
        {
            const int count = std::clamp(p.turbo_count,1,4);
            for (int turbo = 0; turbo < count; ++turbo)
            {
                const auto& mounting = layout.boost[turbo];
                const Vector3 center = vec(mounting.position), axis = vec(mounting.axis);
                const float radius = std::min(mounting.size[0],mounting.size[1])*0.5f;
                const std::string name = p.forced_induction == 0 ? "Turbo " : "Supercharger ";
                disc(name+std::to_string(turbo)+" compressor housing","ring",center,axis,mounting.size[2]*0.4f,radius,0,alloy);
                disc(name+std::to_string(turbo)+" turbine housing","ring",center+axis*mounting.size[2]*0.5f,axis,mounting.size[2]*0.4f,radius*0.87f,0,brake);
                link(name+std::to_string(turbo)+" boost pipe",center,engine+up*(stroke*3.2f),0.025f,alloy);
                const float boost = std::clamp(sim.get_boost_pressure()/std::max(p.boost_max_pressure,0.01f),0.0f,1.0f);
                link(name+std::to_string(turbo)+" wastegate actuator",center+Vector3(0,0.08f,0),center+Vector3(0,0.10f+boost*0.04f,0),0.012f,chrome);
            }
        }
        // Assists have controller state, not separately simulated hydraulic hardware.
        // Explicit status modules avoid implying that their physical internals are solved.
        if (p.abs_enabled || p.tc_enabled || p.yaw_control_enabled)
        {
            const Vector3 ecu = vec(layout.controller.position);
            part("Driver assists / controller","box",ecu,frame(layout.controller),vec(layout.controller.size),steel);
            for (int i = 0; i < 4; ++i)
                box("ABS / wheel channel "+std::to_string(i),ecu+Vector3((static_cast<float>(i)-1.5f)*0.05f,0.04f,0),Vector3(0.03f,0.015f,0.045f),sim.is_abs_active(i) ? spring : alloy);
            box("Traction control / intervention",ecu+Vector3(0.15f,0,0),Vector3(0.03f,0.035f,0.05f),sim.is_tc_active() ? spring : alloy);
        }
        // Aero force application sites come from the solver, not the decorative body.
        const auto& aero = sim.get_aero_debug();
        if (aero.valid)
        {
            if (p.lift_coeff_front != 0)
                box("Aerodynamics / front force surface",local_actor(aero.front_aero_pos),Vector3(cfg.track_front*0.7f,0.012f,0.12f),steel);
            if (p.lift_coeff_rear != 0)
                part("Aerodynamics / rear DRS surface","box",local_actor(aero.rear_aero_pos),Quaternion::FromAxisAngle(Vector3::Right,sim.get_drs_active() ? -0.35f : 0.10f),Vector3(cfg.track_rear*0.7f,0.012f,0.15f),steel);
            if (p.ground_effect_enabled)
                box("Aerodynamics / ground effect surface",Vector3(0,p.underfloor_y,0),Vector3(0.25f,0.012f,cfg.wheelbase*0.5f),steel);
        }
    }
}
