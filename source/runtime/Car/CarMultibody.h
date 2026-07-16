#pragma once

namespace car
{
    static constexpr int max_suspension_members = 8;
    static constexpr int max_suspension_joints  = 96;

    struct suspension_member
    {
        PxRigidDynamic* actor = nullptr;
        PxVec3 local_start    = PxVec3(0.0f);
        PxVec3 local_end      = PxVec3(0.0f);
    };

    struct suspension_corner
    {
        PxRigidDynamic* upright = nullptr;
        PxRigidDynamic* wheel_body = nullptr;
        PxRevoluteJoint* wheel_joint = nullptr;
        PxDistanceJoint* travel_joint = nullptr;
        suspension_member members[max_suspension_members];
        int member_count = 0;
        PxVec3 chassis_shock_anchor = PxVec3(0.0f);
        PxVec3 upright_shock_anchor = PxVec3(0.0f);
        float shock_rest_length = 0.0f;
        float shock_length = 0.0f;
        float shock_velocity = 0.0f;
    };

    struct multibody_state
    {
        PxPhysics* physics = nullptr;
        PxScene* scene = nullptr;
        suspension_corner corners[wheel_count];
        PxRigidDynamic* rack = nullptr;
        PxD6Joint* rack_joint = nullptr;
        PxJoint* joints[max_suspension_joints] = {};
        PxRigidDynamic* actors[wheel_count * (max_suspension_members + 2) + 1] = {};
        int joint_count = 0;
        int actor_count = 0;
        float rack_travel = 0.14f;
        bool initialized = false;
    };

    inline multibody_state multibody;

    struct actor_motion_state
    {
        PxVec3 linear_velocity = PxVec3(0.0f);
        PxVec3 angular_velocity = PxVec3(0.0f);
        bool valid = false;
    };

    struct corner_motion_state
    {
        actor_motion_state upright;
        actor_motion_state wheel;
        actor_motion_state members[max_suspension_members];
        int member_count = 0;
    };

    struct multibody_motion_state
    {
        corner_motion_state corners[wheel_count];
        actor_motion_state rack;
        bool valid = false;
    };

    inline actor_motion_state capture_actor_motion(PxRigidDynamic* actor)
    {
        actor_motion_state state;
        if (!actor || !body)
        {
            return state;
        }

        PxTransform chassis_pose = body->getGlobalPose();
        PxVec3 chassis_angular_velocity = body->getAngularVelocity();
        PxVec3 chassis_point_velocity = body->getLinearVelocity() + chassis_angular_velocity.cross(actor->getGlobalPose().p - chassis_pose.p);
        state.linear_velocity = chassis_pose.q.rotateInv(actor->getLinearVelocity() - chassis_point_velocity);
        state.angular_velocity = chassis_pose.q.rotateInv(actor->getAngularVelocity() - chassis_angular_velocity);
        state.valid = true;
        return state;
    }

    inline void restore_actor_motion(PxRigidDynamic* actor, const actor_motion_state& state)
    {
        if (!actor || !body || !state.valid)
        {
            return;
        }

        PxTransform chassis_pose = body->getGlobalPose();
        PxVec3 chassis_angular_velocity = body->getAngularVelocity();
        PxVec3 chassis_point_velocity = body->getLinearVelocity() + chassis_angular_velocity.cross(actor->getGlobalPose().p - chassis_pose.p);
        actor->setLinearVelocity(chassis_point_velocity + chassis_pose.q.rotate(state.linear_velocity));
        actor->setAngularVelocity(chassis_angular_velocity + chassis_pose.q.rotate(state.angular_velocity));
        actor->wakeUp();
    }

    inline multibody_motion_state capture_multibody_motion()
    {
        multibody_motion_state state;
        if (!multibody.initialized)
        {
            return state;
        }

        for (int i = 0; i < wheel_count; i++)
        {
            suspension_corner& corner = multibody.corners[i];
            corner_motion_state& corner_state = state.corners[i];
            corner_state.upright = capture_actor_motion(corner.upright);
            corner_state.wheel = capture_actor_motion(corner.wheel_body);
            corner_state.member_count = corner.member_count;
            for (int member_index = 0; member_index < corner.member_count; member_index++)
            {
                corner_state.members[member_index] = capture_actor_motion(corner.members[member_index].actor);
            }
        }
        state.rack = capture_actor_motion(multibody.rack);
        state.valid = true;
        return state;
    }

    inline void restore_multibody_motion(const multibody_motion_state& state)
    {
        if (!state.valid || !multibody.initialized)
        {
            return;
        }

        for (int i = 0; i < wheel_count; i++)
        {
            suspension_corner& corner = multibody.corners[i];
            const corner_motion_state& corner_state = state.corners[i];
            restore_actor_motion(corner.upright, corner_state.upright);
            restore_actor_motion(corner.wheel_body, corner_state.wheel);
            int member_count = PxMin(corner.member_count, corner_state.member_count);
            for (int member_index = 0; member_index < member_count; member_index++)
            {
                restore_actor_motion(corner.members[member_index].actor, corner_state.members[member_index]);
            }
        }
        restore_actor_motion(multibody.rack, state.rack);
    }

    inline PxTransform local_anchor(PxRigidActor* actor, const PxVec3& world_point)
    {
        return actor ? PxTransform(actor->getGlobalPose().transformInv(world_point)) : PxTransform(world_point);
    }

    inline void register_multibody_actor(PxRigidDynamic* actor)
    {
        if (actor && multibody.actor_count < static_cast<int>(std::size(multibody.actors)))
        {
            multibody.actors[multibody.actor_count++] = actor;
        }
    }

    inline void register_multibody_joint(PxJoint* joint)
    {
        if (joint && multibody.joint_count < max_suspension_joints)
        {
            multibody.joints[multibody.joint_count++] = joint;
        }
    }

    inline void configure_mechanism_shape(PxShape* shape)
    {
        if (!shape)
        {
            return;
        }

        shape->setFlag(PxShapeFlag::eSIMULATION_SHAPE, false);
        shape->setFlag(PxShapeFlag::eSCENE_QUERY_SHAPE, false);
        shape->setFlag(PxShapeFlag::eVISUALIZATION, true);
    }

    inline PxRigidDynamic* create_mechanism_actor(const PxTransform& pose, const PxGeometry& geometry, float mass)
    {
        PxRigidDynamic* actor = multibody.physics->createRigidDynamic(pose);
        if (!actor)
        {
            return nullptr;
        }

        PxShape* shape = multibody.physics->createShape(geometry, *material);
        if (!shape)
        {
            actor->release();
            return nullptr;
        }

        configure_mechanism_shape(shape);
        actor->attachShape(*shape);
        shape->release();
        PxRigidBodyExt::setMassAndUpdateInertia(*actor, PxMax(mass, 0.1f));
        actor->setSolverIterationCounts(16, 4);
        actor->setRigidBodyFlag(PxRigidBodyFlag::eENABLE_CCD, true);
        multibody.scene->addActor(*actor);
        register_multibody_actor(actor);
        return actor;
    }

    inline PxSphericalJoint* create_spherical_joint(PxRigidActor* actor_a, PxRigidActor* actor_b, const PxVec3& world_anchor)
    {
        PxSphericalJoint* joint = PxSphericalJointCreate(*multibody.physics, actor_a, local_anchor(actor_a, world_anchor), actor_b, local_anchor(actor_b, world_anchor));
        if (joint)
        {
            joint->setConstraintFlag(PxConstraintFlag::eENABLE_EXTENDED_LIMITS, true);
            register_multibody_joint(joint);
        }
        return joint;
    }

    inline PxRigidDynamic* create_link(const PxVec3& world_start, const PxVec3& world_end, float mass)
    {
        PxVec3 delta = world_end - world_start;
        float length = delta.magnitude();
        if (length < 0.02f)
        {
            return nullptr;
        }

        PxVec3 direction = delta / length;
        PxVec3 cross = PxVec3(1.0f, 0.0f, 0.0f).cross(direction);
        float dot = PxVec3(1.0f, 0.0f, 0.0f).dot(direction);
        PxQuat rotation = dot < -0.9999f ? PxQuat(PxPi, PxVec3(0.0f, 1.0f, 0.0f)) : PxQuat(cross.x, cross.y, cross.z, 1.0f + dot).getNormalized();
        float radius = 0.018f;
        PxCapsuleGeometry geometry(radius, PxMax(length * 0.5f - radius, 0.01f));
        PxRigidDynamic* actor = create_mechanism_actor(PxTransform((world_start + world_end) * 0.5f, rotation), geometry, mass);
        if (actor)
        {
            create_spherical_joint(body, actor, world_start);
        }
        return actor;
    }

    inline void connect_link_to_upright(PxRigidDynamic* link, PxRigidDynamic* upright, const PxVec3& world_anchor)
    {
        if (link && upright)
        {
            create_spherical_joint(link, upright, world_anchor);
        }
    }

    inline void add_link_member(suspension_corner& corner, const PxVec3& world_start, const PxVec3& world_end, float mass, PxRigidDynamic* start_actor = nullptr)
    {
        if (corner.member_count >= max_suspension_members)
        {
            return;
        }

        PxRigidDynamic* link = create_link(world_start, world_end, mass);
        if (!link)
        {
            return;
        }

        if (start_actor)
        {
            PxJoint* chassis_joint = multibody.joints[--multibody.joint_count];
            chassis_joint->release();
            create_spherical_joint(start_actor, link, world_start);
        }
        connect_link_to_upright(link, corner.upright, world_end);
        suspension_member& member = corner.members[corner.member_count++];
        member.actor = link;
        member.local_start = link->getGlobalPose().transformInv(world_start);
        member.local_end = link->getGlobalPose().transformInv(world_end);
    }

    inline void add_wishbone(suspension_corner& corner, const PxVec3& inner_front, const PxVec3& inner_rear, const PxVec3& outer, float mass)
    {
        if (corner.member_count > max_suspension_members - 2)
        {
            return;
        }

        PxVec3 center = (inner_front + inner_rear + outer) / 3.0f;
        PxVec3 extents = PxVec3(PxMax(fabsf(outer.x - inner_front.x) * 0.5f, 0.03f), 0.018f, PxMax(fabsf(inner_front.z - inner_rear.z) * 0.5f, 0.03f));
        PxRigidDynamic* arm = create_mechanism_actor(PxTransform(center), PxBoxGeometry(extents), mass);
        if (!arm)
        {
            return;
        }

        create_spherical_joint(body, arm, inner_front);
        create_spherical_joint(body, arm, inner_rear);
        create_spherical_joint(arm, corner.upright, outer);
        suspension_member& front_member = corner.members[corner.member_count++];
        front_member.actor = arm;
        front_member.local_start = arm->getGlobalPose().transformInv(inner_front);
        front_member.local_end = arm->getGlobalPose().transformInv(outer);
        suspension_member& rear_member = corner.members[corner.member_count++];
        rear_member.actor = arm;
        rear_member.local_start = arm->getGlobalPose().transformInv(inner_rear);
        rear_member.local_end = arm->getGlobalPose().transformInv(outer);
    }

    inline bool add_macpherson_strut(suspension_corner& corner, const PxVec3& top, const PxVec3& bottom, float mass)
    {
        if (corner.member_count >= max_suspension_members)
        {
            return false;
        }

        PxRigidDynamic* strut = create_link(top, bottom, mass);
        if (!strut)
        {
            return false;
        }

        PxTransform strut_pose = strut->getGlobalPose();
        PxTransform upright_pose = corner.upright->getGlobalPose();
        PxTransform strut_frame(strut_pose.transformInv(bottom), PxQuat(PxIdentity));
        PxTransform upright_frame(upright_pose.transformInv(bottom), upright_pose.q.getConjugate() * strut_pose.q);
        PxD6Joint* slider = PxD6JointCreate(*multibody.physics, strut, strut_frame, corner.upright, upright_frame);
        if (!slider)
        {
            return false;
        }

        slider->setMotion(PxD6Axis::eX, PxD6Motion::eLIMITED);
        slider->setMotion(PxD6Axis::eY, PxD6Motion::eLOCKED);
        slider->setMotion(PxD6Axis::eZ, PxD6Motion::eLOCKED);
        slider->setMotion(PxD6Axis::eTWIST, PxD6Motion::eFREE);
        slider->setMotion(PxD6Axis::eSWING1, PxD6Motion::eLOCKED);
        slider->setMotion(PxD6Axis::eSWING2, PxD6Motion::eLOCKED);
        slider->setLinearLimit(PxD6Axis::eX, PxJointLinearLimitPair(multibody.physics->getTolerancesScale(), -cfg.suspension_travel, cfg.suspension_travel));
        register_multibody_joint(slider);

        suspension_member& member = corner.members[corner.member_count++];
        member.actor = strut;
        member.local_start = strut_pose.transformInv(top);
        member.local_end = strut_pose.transformInv(bottom);
        return true;
    }

    inline bool add_steering_stop(PxRigidDynamic* upright, const PxTransform& chassis_pose, const PxVec3& wheel_world, float angle_limit)
    {
        PxQuat axis_frame(PxPi * 0.5f, PxVec3(0.0f, 0.0f, 1.0f));
        PxQuat world_frame_rotation = chassis_pose.q * axis_frame;
        PxTransform chassis_frame(chassis_pose.transformInv(wheel_world), axis_frame);
        PxTransform upright_pose = upright->getGlobalPose();
        PxTransform upright_frame(upright_pose.transformInv(wheel_world), upright_pose.q.getConjugate() * world_frame_rotation);
        PxD6Joint* stop = PxD6JointCreate(*multibody.physics, body, chassis_frame, upright, upright_frame);
        if (!stop)
        {
            return false;
        }

        stop->setMotion(PxD6Axis::eX, PxD6Motion::eFREE);
        stop->setMotion(PxD6Axis::eY, PxD6Motion::eFREE);
        stop->setMotion(PxD6Axis::eZ, PxD6Motion::eFREE);
        stop->setMotion(PxD6Axis::eSWING1, PxD6Motion::eFREE);
        stop->setMotion(PxD6Axis::eSWING2, PxD6Motion::eFREE);
        if (angle_limit > 0.0f)
        {
            stop->setMotion(PxD6Axis::eTWIST, PxD6Motion::eLIMITED);
            stop->setTwistLimit(PxJointAngularLimitPair(-angle_limit, angle_limit));
        }
        else
        {
            stop->setMotion(PxD6Axis::eTWIST, PxD6Motion::eLOCKED);
        }
        register_multibody_joint(stop);
        return true;
    }

    inline PxVec3 hardpoint_world(const PxTransform& chassis_pose, const PxVec3& local_point)
    {
        return chassis_pose.transform(local_point);
    }

    inline float mechanism_actor_mass()
    {
        auto link_count = [](suspension_mechanism mechanism)
        {
            if (mechanism == suspension_mechanism::multi_link)
            {
                return 4;
            }
            return 2;
        };

        int suspension_links = link_count(tuning::spec.front_geometry.mechanism) * 2 + link_count(tuning::spec.rear_geometry.mechanism) * 2 + 2;
        return (cfg.wheel_mass + tuning::spec.upright_mass) * static_cast<float>(wheel_count) + tuning::spec.suspension_link_mass * static_cast<float>(suspension_links) + tuning::spec.steering_rack_mass;
    }

    inline float chassis_mass()
    {
        return PxMax(cfg.mass - mechanism_actor_mass(), 100.0f);
    }

    inline bool create_suspension_corner(int wheel_index, const suspension_geometry& geometry)
    {
        suspension_corner& corner = multibody.corners[wheel_index];
        PxTransform chassis_pose = body->getGlobalPose();
        PxVec3 wheel_local = wheel_offsets[wheel_index];
        PxVec3 wheel_world = hardpoint_world(chassis_pose, wheel_local);
        float side = wheel_local.x < 0.0f ? -1.0f : 1.0f;
        float inner_x = wheel_local.x * geometry.chassis_inset;
        float arm_span = PxMax(geometry.arm_span, 0.05f);
        float camber = is_front(wheel_index) ? tuning::spec.front_camber : tuning::spec.rear_camber;
        float toe = is_front(wheel_index) ? tuning::spec.front_toe : tuning::spec.rear_toe;
        PxQuat alignment = PxQuat(side * toe, PxVec3(0.0f, 1.0f, 0.0f)) * PxQuat(side * camber, PxVec3(0.0f, 0.0f, 1.0f));
        PxTransform wheel_pose(wheel_world, chassis_pose.q * alignment);

        corner.upright = create_mechanism_actor(wheel_pose, PxBoxGeometry(0.04f, 0.18f, 0.06f), tuning::spec.upright_mass);
        corner.wheel_body = create_mechanism_actor(wheel_pose, PxSphereGeometry(cfg.wheel_radius_for(wheel_index)), cfg.wheel_mass);
        if (!corner.upright || !corner.wheel_body)
        {
            return false;
        }
        float wheel_inertia = PxMax(wheel_moi[wheel_index], 0.1f);
        corner.wheel_body->setMassSpaceInertiaTensor(PxVec3(wheel_inertia, wheel_inertia * 0.65f, wheel_inertia * 0.65f));

        corner.wheel_joint = PxRevoluteJointCreate(*multibody.physics, corner.upright, PxTransform(PxIdentity), corner.wheel_body, PxTransform(PxIdentity));
        if (!corner.wheel_joint)
        {
            return false;
        }
        corner.wheel_joint->setConstraintFlag(PxConstraintFlag::eENABLE_EXTENDED_LIMITS, true);
        register_multibody_joint(corner.wheel_joint);
        float steering_limit = is_front(wheel_index) ? PxClamp(fabsf(tuning::spec.max_steer_angle), 0.1f, 1.2f) : 0.0f;
        if (!add_steering_stop(corner.upright, chassis_pose, wheel_world, steering_limit))
        {
            return false;
        }

        PxVec3 upper_outer_local = wheel_local + PxVec3(-side * 0.015f, geometry.upper_upright_y, 0.0f);
        PxVec3 lower_outer_local = wheel_local + PxVec3(-side * 0.015f, geometry.lower_upright_y, 0.0f);
        PxVec3 upper_inner_front_local(inner_x, wheel_local.y + geometry.upper_inner_y, wheel_local.z + arm_span);
        PxVec3 upper_inner_rear_local(inner_x, wheel_local.y + geometry.upper_inner_y, wheel_local.z - arm_span);
        PxVec3 lower_inner_front_local(inner_x, wheel_local.y + geometry.lower_inner_y, wheel_local.z + arm_span);
        PxVec3 lower_inner_rear_local(inner_x, wheel_local.y + geometry.lower_inner_y, wheel_local.z - arm_span);

        if (geometry.mechanism == suspension_mechanism::multi_link)
        {
            float spread_y = PxMax(geometry.link_spread_y, 0.05f);
            float spread_z = PxMax(geometry.link_spread_z, 0.05f);
            const PxVec3 inner_points[4] =
            {
                PxVec3(inner_x, wheel_local.y + geometry.upper_inner_y, wheel_local.z + spread_z),
                PxVec3(inner_x, wheel_local.y + geometry.upper_inner_y, wheel_local.z - spread_z),
                PxVec3(inner_x, wheel_local.y + geometry.lower_inner_y, wheel_local.z + spread_z),
                PxVec3(inner_x, wheel_local.y + geometry.lower_inner_y, wheel_local.z - spread_z)
            };
            const PxVec3 outer_points[4] =
            {
                wheel_local + PxVec3(0.0f, spread_y, spread_z * 0.45f),
                wheel_local + PxVec3(0.0f, spread_y, -spread_z * 0.45f),
                wheel_local + PxVec3(0.0f, -spread_y, spread_z * 0.45f),
                wheel_local + PxVec3(0.0f, -spread_y, -spread_z * 0.45f)
            };
            for (int i = 0; i < 4; i++)
            {
                add_link_member(corner, hardpoint_world(chassis_pose, inner_points[i]), hardpoint_world(chassis_pose, outer_points[i]), tuning::spec.suspension_link_mass);
            }
        }
        else
        {
            add_wishbone(corner, hardpoint_world(chassis_pose, lower_inner_front_local), hardpoint_world(chassis_pose, lower_inner_rear_local), hardpoint_world(chassis_pose, lower_outer_local), tuning::spec.suspension_link_mass);
            if (geometry.mechanism == suspension_mechanism::double_wishbone)
            {
                add_wishbone(corner, hardpoint_world(chassis_pose, upper_inner_front_local), hardpoint_world(chassis_pose, upper_inner_rear_local), hardpoint_world(chassis_pose, upper_outer_local), tuning::spec.suspension_link_mass);
            }
        }

        PxVec3 shock_top_local(side * (fabsf(wheel_local.x) - geometry.strut_top_inset), wheel_local.y + geometry.strut_top_y, wheel_local.z);
        PxVec3 shock_bottom_local = wheel_local + PxVec3(0.0f, geometry.lower_upright_y, 0.0f);
        PxVec3 shock_top_world = hardpoint_world(chassis_pose, shock_top_local);
        PxVec3 shock_bottom_world = hardpoint_world(chassis_pose, shock_bottom_local);
        if (geometry.mechanism == suspension_mechanism::macpherson && !add_macpherson_strut(corner, shock_top_world, shock_bottom_world, tuning::spec.suspension_link_mass))
        {
            return false;
        }
        corner.chassis_shock_anchor = shock_top_local;
        corner.upright_shock_anchor = corner.upright->getGlobalPose().transformInv(shock_bottom_world);
        float static_load = chassis_mass() * (is_front(wheel_index) ? get_weight_distribution_front() : 1.0f - get_weight_distribution_front()) * 0.5f * 9.81f;
        corner.shock_rest_length = (shock_top_world - shock_bottom_world).magnitude() + static_load / PxMax(spring_stiffness[wheel_index], 1.0f);
        corner.shock_length = (shock_top_world - shock_bottom_world).magnitude();

        corner.travel_joint = geometry.mechanism == suspension_mechanism::macpherson ? nullptr : PxDistanceJointCreate(*multibody.physics, body, local_anchor(body, shock_top_world), corner.upright, local_anchor(corner.upright, shock_bottom_world));
        if (corner.travel_joint)
        {
            corner.travel_joint->setMinDistance(PxMax(corner.shock_rest_length - cfg.suspension_travel, 0.05f));
            corner.travel_joint->setMaxDistance(corner.shock_rest_length + cfg.suspension_travel * 0.15f);
            corner.travel_joint->setDistanceJointFlag(PxDistanceJointFlag::eMIN_DISTANCE_ENABLED, true);
            corner.travel_joint->setDistanceJointFlag(PxDistanceJointFlag::eMAX_DISTANCE_ENABLED, true);
            register_multibody_joint(corner.travel_joint);
        }

        return true;
    }

    inline bool create_locked_differential(int left, int right)
    {
        suspension_corner& left_corner = multibody.corners[left];
        suspension_corner& right_corner = multibody.corners[right];
        PxGearJoint* differential = PxGearJointCreate(*multibody.physics, left_corner.wheel_body, PxTransform(PxIdentity), right_corner.wheel_body, PxTransform(PxIdentity));
        if (!differential || !differential->setHinges(left_corner.wheel_joint, right_corner.wheel_joint))
        {
            if (differential)
            {
                differential->release();
            }
            return false;
        }

        differential->setGearRatio(-1.0f);
        register_multibody_joint(differential);
        return true;
    }

    inline bool create_steering_rack()
    {
        PxTransform chassis_pose = body->getGlobalPose();
        multibody.rack_travel = PxClamp(tanf(fabsf(tuning::spec.max_steer_angle)) * 0.22f, 0.05f, 0.20f);
        float front_z = wheel_offsets[front_left].z + tuning::spec.front_geometry.tie_rod_z;
        float rack_y = wheel_offsets[front_left].y + tuning::spec.front_geometry.tie_rod_y;
        PxVec3 rack_world = hardpoint_world(chassis_pose, PxVec3(0.0f, rack_y, front_z));
        multibody.rack = create_mechanism_actor(PxTransform(rack_world, chassis_pose.q), PxBoxGeometry(cfg.track_front * 0.35f, 0.025f, 0.025f), tuning::spec.steering_rack_mass);
        if (!multibody.rack)
        {
            return false;
        }

        multibody.rack_joint = PxD6JointCreate(*multibody.physics, body, local_anchor(body, rack_world), multibody.rack, PxTransform(PxIdentity));
        if (!multibody.rack_joint)
        {
            return false;
        }
        multibody.rack_joint->setMotion(PxD6Axis::eX, PxD6Motion::eLIMITED);
        multibody.rack_joint->setMotion(PxD6Axis::eY, PxD6Motion::eLOCKED);
        multibody.rack_joint->setMotion(PxD6Axis::eZ, PxD6Motion::eLOCKED);
        multibody.rack_joint->setMotion(PxD6Axis::eTWIST, PxD6Motion::eLOCKED);
        multibody.rack_joint->setMotion(PxD6Axis::eSWING1, PxD6Motion::eLOCKED);
        multibody.rack_joint->setMotion(PxD6Axis::eSWING2, PxD6Motion::eLOCKED);
        multibody.rack_joint->setLinearLimit(PxD6Axis::eX, PxJointLinearLimitPair(multibody.physics->getTolerancesScale(), -multibody.rack_travel, multibody.rack_travel));
        multibody.rack_joint->setDrive(PxD6Drive::eX, PxD6JointDrive(18000.0f, 1800.0f, PX_MAX_F32, true));
        register_multibody_joint(multibody.rack_joint);

        for (int wheel_index : { front_left, front_right })
        {
            suspension_corner& corner = multibody.corners[wheel_index];
            float side = wheel_index == front_left ? -1.0f : 1.0f;
            PxVec3 rack_end_world = hardpoint_world(chassis_pose, PxVec3(side * cfg.track_front * 0.35f, rack_y, front_z));
            PxVec3 upright_anchor_world = hardpoint_world(chassis_pose, wheel_offsets[wheel_index] + PxVec3(0.0f, tuning::spec.front_geometry.tie_rod_y, tuning::spec.front_geometry.tie_rod_z));
            add_link_member(corner, rack_end_world, upright_anchor_world, tuning::spec.suspension_link_mass, multibody.rack);
        }
        return true;
    }

    inline void destroy_multibody()
    {
        for (int i = multibody.joint_count - 1; i >= 0; i--)
        {
            if (multibody.joints[i])
            {
                multibody.joints[i]->release();
            }
        }
        for (int i = multibody.actor_count - 1; i >= 0; i--)
        {
            if (multibody.actors[i])
            {
                multibody.actors[i]->release();
            }
        }
        multibody = multibody_state();
    }

    inline bool create_multibody(PxPhysics* physics, PxScene* scene)
    {
        if (!body || !physics || !scene || !material)
        {
            return false;
        }

        destroy_multibody();
        multibody.physics = physics;
        multibody.scene = scene;
        body->setActorFlag(PxActorFlag::eDISABLE_GRAVITY, false);
        body->setSolverIterationCounts(16, 4);

        for (int i = 0; i < wheel_count; i++)
        {
            const suspension_geometry& geometry = is_front(i) ? tuning::spec.front_geometry : tuning::spec.rear_geometry;
            if (!create_suspension_corner(i, geometry))
            {
                destroy_multibody();
                return false;
            }
        }
        if (tuning::spec.diff_type == 1)
        {
            bool lock_front = tuning::spec.drivetrain_type == 1 || tuning::spec.drivetrain_type == 2;
            bool lock_rear = tuning::spec.drivetrain_type == 0 || tuning::spec.drivetrain_type == 2;
            if ((lock_front && !create_locked_differential(front_left, front_right)) || (lock_rear && !create_locked_differential(rear_left, rear_right)))
            {
                destroy_multibody();
                return false;
            }
        }
        if (!create_steering_rack())
        {
            destroy_multibody();
            return false;
        }

        multibody.initialized = true;
        return true;
    }

    inline void sleep_vehicle_assembly();

    inline bool rebuild_multibody(bool preserve_motion = true)
    {
        PxPhysics* physics = multibody.physics;
        PxScene* scene = multibody.scene;
        multibody_motion_state motion = preserve_motion ? capture_multibody_motion() : multibody_motion_state();
        bool was_sleeping = preserve_motion && vehicle_sleeping;
        if (!physics || !scene || !create_multibody(physics, scene))
        {
            return false;
        }
        restore_multibody_motion(motion);
        if (was_sleeping)
        {
            sleep_vehicle_assembly();
        }
        return true;
    }

    inline PxVec3 actor_point_velocity(PxRigidBody* actor, const PxVec3& world_point)
    {
        return actor->getLinearVelocity() + actor->getAngularVelocity().cross(world_point - actor->getGlobalPose().p);
    }

    inline void refresh_wheel_actor_state()
    {
        PxVec3 fallback_axis = body ? body->getGlobalPose().q.rotate(PxVec3(1.0f, 0.0f, 0.0f)) : PxVec3(1.0f, 0.0f, 0.0f);
        for (int i = 0; i < wheel_count; i++)
        {
            PxRigidDynamic* wheel_actor = multibody.corners[i].wheel_body;
            PxVec3 wheel_axis = wheel_actor ? wheel_actor->getGlobalPose().q.rotate(PxVec3(1.0f, 0.0f, 0.0f)) : fallback_axis;
            wheels[i].angular_velocity = wheel_actor ? wheel_actor->getAngularVelocity().dot(wheel_axis) : 0.0f;
        }
    }

    inline void wake_vehicle_assembly()
    {
        if (body)
        {
            body->wakeUp();
        }
        for (int i = 0; i < multibody.actor_count; i++)
        {
            if (multibody.actors[i])
            {
                multibody.actors[i]->wakeUp();
            }
        }
        vehicle_sleep_timer = 0.0f;
        vehicle_sleeping = false;
    }

    inline bool vehicle_assembly_is_settled()
    {
        if (!body || body->getLinearVelocity().magnitudeSquared() > 0.0025f || body->getAngularVelocity().magnitudeSquared() > 0.0025f)
        {
            return false;
        }
        for (int i = 0; i < wheel_count; i++)
        {
            if (fabsf(wheels[i].angular_velocity) > 0.1f)
            {
                return false;
            }
        }
        for (int i = 0; i < multibody.actor_count; i++)
        {
            PxRigidDynamic* actor = multibody.actors[i];
            if (actor && (actor->getLinearVelocity().magnitudeSquared() > 0.01f || actor->getAngularVelocity().magnitudeSquared() > 0.0625f))
            {
                return false;
            }
        }
        return true;
    }

    inline void sleep_vehicle_assembly()
    {
        for (int i = 0; i < multibody.actor_count; i++)
        {
            if (multibody.actors[i])
            {
                multibody.actors[i]->putToSleep();
            }
        }
        if (body)
        {
            body->putToSleep();
        }
        for (int i = 0; i < wheel_count; i++)
        {
            wheels[i].angular_velocity = 0.0f;
            wheels[i].net_torque = 0.0f;
        }
        prev_velocity = PxVec3(0.0f);
        vehicle_sleep_timer = 0.0f;
        vehicle_sleeping = true;
    }

    inline void update_multibody(float delta_time)
    {
        if (!multibody.initialized || delta_time <= 0.0f)
        {
            return;
        }

        if (multibody.rack_joint)
        {
            float curved_input = copysignf(powf(fabsf(PxClamp(input.steering, -1.0f, 1.0f)), tuning::spec.steering_linearity), input.steering);
            float rack_target = -curved_input * multibody.rack_travel;
            multibody.rack_joint->setDrivePosition(PxTransform(PxVec3(rack_target, 0.0f, 0.0f)));
        }

        for (int i = 0; i < wheel_count; i++)
        {
            suspension_corner& corner = multibody.corners[i];
            PxVec3 top = body->getGlobalPose().transform(corner.chassis_shock_anchor);
            PxVec3 bottom = corner.upright->getGlobalPose().transform(corner.upright_shock_anchor);
            PxVec3 delta = top - bottom;
            float length = PxMax(delta.magnitude(), 0.001f);
            PxVec3 direction = delta / length;
            float relative_speed = PxClamp((actor_point_velocity(body, top) - actor_point_velocity(corner.upright, bottom)).dot(direction), -tuning::spec.max_damper_velocity, tuning::spec.max_damper_velocity);
            float compression = corner.shock_rest_length - length;
            float damper = spring_damping[i] * (relative_speed < 0.0f ? tuning::spec.damping_bump_ratio : tuning::spec.damping_rebound_ratio);
            float force_magnitude = spring_stiffness[i] * compression - damper * relative_speed;
            float bump_start = cfg.suspension_travel * tuning::spec.bump_stop_threshold;
            if (compression > bump_start)
            {
                force_magnitude += (compression - bump_start) * tuning::spec.bump_stop_stiffness;
            }
            force_magnitude = PxClamp(force_magnitude, 0.0f, tuning::spec.max_susp_force);
            PxVec3 force = direction * force_magnitude;
            PxRigidBodyExt::addForceAtPos(*body, force, top, PxForceMode::eFORCE);
            PxRigidBodyExt::addForceAtPos(*corner.upright, -force, bottom, PxForceMode::eFORCE);

            corner.shock_velocity = (length - corner.shock_length) / delta_time;
            corner.shock_length = length;
            wheels[i].compression = PxClamp(compression / PxMax(cfg.suspension_travel, 0.01f), 0.0f, 1.5f);
            wheels[i].compression_velocity = -corner.shock_velocity / PxMax(cfg.suspension_travel, 0.01f);
            spring_force[i] = force_magnitude;
        }

        auto apply_anti_roll = [&](int left, int right, float stiffness)
        {
            suspension_corner& left_corner = multibody.corners[left];
            suspension_corner& right_corner = multibody.corners[right];
            float left_compression = left_corner.shock_rest_length - left_corner.shock_length;
            float right_compression = right_corner.shock_rest_length - right_corner.shock_length;
            float force_magnitude = PxClamp((left_compression - right_compression) * stiffness, -tuning::spec.max_susp_force, tuning::spec.max_susp_force);
            PxVec3 up = body->getGlobalPose().q.rotate(PxVec3(0.0f, 1.0f, 0.0f));
            PxVec3 left_bottom = left_corner.upright->getGlobalPose().transform(left_corner.upright_shock_anchor);
            PxVec3 right_bottom = right_corner.upright->getGlobalPose().transform(right_corner.upright_shock_anchor);
            PxVec3 left_top = body->getGlobalPose().transform(left_corner.chassis_shock_anchor);
            PxVec3 right_top = body->getGlobalPose().transform(right_corner.chassis_shock_anchor);
            PxVec3 left_force = -up * force_magnitude;
            PxVec3 right_force = up * force_magnitude;
            PxRigidBodyExt::addForceAtPos(*left_corner.upright, left_force, left_bottom, PxForceMode::eFORCE);
            PxRigidBodyExt::addForceAtPos(*right_corner.upright, right_force, right_bottom, PxForceMode::eFORCE);
            PxRigidBodyExt::addForceAtPos(*body, -left_force, left_top, PxForceMode::eFORCE);
            PxRigidBodyExt::addForceAtPos(*body, -right_force, right_top, PxForceMode::eFORCE);
        };
        apply_anti_roll(front_left, front_right, tuning::spec.front_arb_stiffness);
        apply_anti_roll(rear_left, rear_right, tuning::spec.rear_arb_stiffness);
    }
}
