#pragma once

#include "engine/physics/jolt_setup.h"
#include <Jolt/Core/JobSystemSingleThreaded.h>
#include <memory>

// Wraps Jolt's PhysicsSystem and its required infrastructure.
// Stored in registry context so systems can access it.
struct JoltWorld
{
    // Jolt infrastructure — must outlive the PhysicsSystem
    std::unique_ptr<JPH::TempAllocatorImpl> tempAllocator;
    std::unique_ptr<JPH::JobSystem> jobSystem;

    // Layer interfaces
    BPLayerInterfaceImpl broadPhaseLayerInterface;
    ObjectVsBroadPhaseLayerFilterImpl objectVsBroadPhaseFilter;
    ObjectLayerPairFilterImpl objectLayerPairFilter;

    // The physics world itself
    std::unique_ptr<JPH::PhysicsSystem> physicsSystem;

    // singleThreaded=true uses a deterministic single-threaded job system —
    // required by the headless harness so simulation runs are reproducible.
    // `gravity` is the downward magnitude (units/s^2); pass PhysicsConfig::gravity
    // so the world and the player system share one value.
    void init(bool singleThreaded = false, float gravity = 20.0f)
    {
        // Register Jolt allocator and install callbacks
        JPH::RegisterDefaultAllocator();
        JPH::Trace = JoltTraceImpl;
        JPH_IF_ENABLE_ASSERTS(JPH::AssertFailed = JoltAssertFailedImpl;)

        // Create the factory (needed for serialization/deserialization)
        JPH::Factory::sInstance = new JPH::Factory();
        JPH::RegisterTypes();

        // Pre-allocate 10 MB for physics temp data
        tempAllocator = std::make_unique<JPH::TempAllocatorImpl>(10 * 1024 * 1024);

        // Job system: deterministic single-threaded for headless, otherwise a
        // thread pool sized to all available cores minus one.
        if (singleThreaded)
        {
            jobSystem = std::make_unique<JPH::JobSystemSingleThreaded>(JPH::cMaxPhysicsJobs);
        }
        else
        {
            jobSystem = std::make_unique<JPH::JobSystemThreadPool>(
                JPH::cMaxPhysicsJobs, JPH::cMaxPhysicsBarriers,
                (int)std::thread::hardware_concurrency() - 1
            );
        }

        // Create the physics system
        const uint maxBodies = 1024;
        const uint numBodyMutexes = 0;    // auto
        const uint maxBodyPairs = 1024;
        const uint maxContactConstraints = 1024;

        physicsSystem = std::make_unique<JPH::PhysicsSystem>();
        physicsSystem->Init(
            maxBodies, numBodyMutexes, maxBodyPairs, maxContactConstraints,
            broadPhaseLayerInterface, objectVsBroadPhaseFilter,
            objectLayerPairFilter
        );

        // Set gravity (Quake-style, magnitude from PhysicsConfig — default 20 units/s^2 downward)
        physicsSystem->SetGravity(JPH::Vec3(0.0f, -gravity, 0.0f));
    }

    void step(float deltaTime)
    {
        // Step the physics simulation
        // 1 collision step per update is fine for our fixed timestep
        physicsSystem->Update(deltaTime, 1, tempAllocator.get(), jobSystem.get());
    }

    JPH::BodyInterface& getBodyInterface()
    {
        return physicsSystem->GetBodyInterface();
    }

    void shutdown()
    {
        physicsSystem.reset();
        jobSystem.reset();
        tempAllocator.reset();

        JPH::UnregisterTypes();
        delete JPH::Factory::sInstance;
        JPH::Factory::sInstance = nullptr;
    }
};