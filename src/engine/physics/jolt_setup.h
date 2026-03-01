#pragma once

// Jolt requires this macro before including any headers
#include <Jolt/Jolt.h>

// Jolt headers
#include <Jolt/RegisterTypes.h>
#include <Jolt/Core/Factory.h>
#include <Jolt/Core/TempAllocator.h>
#include <Jolt/Core/JobSystemThreadPool.h>
#include <Jolt/Physics/PhysicsSettings.h>
#include <Jolt/Physics/PhysicsSystem.h>
#include <Jolt/Physics/Collision/Shape/BoxShape.h>
#include <Jolt/Physics/Collision/Shape/CapsuleShape.h>
#include <Jolt/Physics/Body/BodyCreationSettings.h>
#include <Jolt/Physics/Body/BodyActivationListener.h>

#include <thread>
#include <cstdarg>
#include <iostream>

// All Jolt symbols are inside the JPH namespace
using namespace JPH;
using namespace JPH::literals;

// ─── Object Layers ──────────────────────────────────────────
// These map roughly to our existing CollisionLayers bitmask,
// but Jolt uses a different system: ObjectLayers for bodies,
// BroadPhaseLayers for the broad-phase acceleration structure.

namespace Layers
{
    static constexpr JPH::ObjectLayer NON_MOVING = 0;  // floors, walls
    static constexpr JPH::ObjectLayer MOVING     = 1;  // player, physics objects
    static constexpr JPH::ObjectLayer SENSOR     = 2;  // triggers (no collision response)
    static constexpr JPH::ObjectLayer NUM_LAYERS = 3;
};

namespace BroadPhaseLayers
{
    static constexpr JPH::BroadPhaseLayer NON_MOVING(0);
    static constexpr JPH::BroadPhaseLayer MOVING(1);
    static constexpr uint NUM_LAYERS(2);
};

// ─── Layer Mapping ──────────────────────────────────────────
// Maps each ObjectLayer to a BroadPhaseLayer.
// Non-moving and sensors share the static broad-phase layer.

class BPLayerInterfaceImpl final : public JPH::BroadPhaseLayerInterface
{
public:
    BPLayerInterfaceImpl()
    {
        mObjectToBroadPhase[Layers::NON_MOVING] = BroadPhaseLayers::NON_MOVING;
        mObjectToBroadPhase[Layers::MOVING]     = BroadPhaseLayers::MOVING;
        mObjectToBroadPhase[Layers::SENSOR]     = BroadPhaseLayers::NON_MOVING;
    }

    virtual uint GetNumBroadPhaseLayers() const override
    {
        return BroadPhaseLayers::NUM_LAYERS;
    }

    virtual JPH::BroadPhaseLayer GetBroadPhaseLayer(JPH::ObjectLayer inLayer) const override
    {
        return mObjectToBroadPhase[inLayer];
    }

    virtual const char* GetBroadPhaseLayerName(JPH::BroadPhaseLayer inLayer) const override
    {
        switch ((JPH::BroadPhaseLayer::Type)inLayer)
        {
        case (JPH::BroadPhaseLayer::Type)BroadPhaseLayers::NON_MOVING: return "NON_MOVING";
        case (JPH::BroadPhaseLayer::Type)BroadPhaseLayers::MOVING:     return "MOVING";
        default:                                                        return "UNKNOWN";
        }
    }

private:
    JPH::BroadPhaseLayer mObjectToBroadPhase[Layers::NUM_LAYERS];
};

// ─── Collision Filters ──────────────────────────────────────
// Determines which object layers can collide with each other.
// Static objects don't collide with each other (no point).

class ObjectLayerPairFilterImpl : public JPH::ObjectLayerPairFilter
{
public:
    virtual bool ShouldCollide(JPH::ObjectLayer inLayer1,
                               JPH::ObjectLayer inLayer2) const override
    {
        switch (inLayer1)
        {
        case Layers::NON_MOVING:
            return inLayer2 == Layers::MOVING;
        case Layers::MOVING:
            return inLayer2 != Layers::SENSOR;
        case Layers::SENSOR:
            return inLayer2 == Layers::MOVING;
        default:
            return false;
        }
    }
};

class ObjectVsBroadPhaseLayerFilterImpl : public JPH::ObjectVsBroadPhaseLayerFilter
{
public:
    virtual bool ShouldCollide(JPH::ObjectLayer inLayer1,
                               JPH::BroadPhaseLayer inLayer2) const override
    {
        switch (inLayer1)
        {
        case Layers::NON_MOVING:
            return inLayer2 == BroadPhaseLayers::MOVING;
        case Layers::MOVING:
            return true;
        case Layers::SENSOR:
            return inLayer2 == BroadPhaseLayers::MOVING;
        default:
            return false;
        }
    }
};

// ─── Jolt Trace Callback (debug logging) ────────────────────

static void JoltTraceImpl(const char* inFMT, ...)
{
    va_list list;
    va_start(list, inFMT);
    char buffer[1024];
    vsnprintf(buffer, sizeof(buffer), inFMT, list);
    va_end(list);
    std::cout << "[Jolt] " << buffer << std::endl;
}

#ifdef JPH_ENABLE_ASSERTS
static bool JoltAssertFailedImpl(const char* inExpression, const char* inMessage,
                                  const char* inFile, uint inLine)
{
    std::cout << inFile << ":" << inLine << ": (" << inExpression << ") "
              << (inMessage != nullptr ? inMessage : "") << std::endl;
    return true;  // break into debugger
}
#endif