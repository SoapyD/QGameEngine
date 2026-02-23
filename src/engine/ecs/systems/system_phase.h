#pragma once

// Defines the conceptual phases of the game loop.
// Systems should run in this order. This enum exists for documentation
// and future use (e.g. a scheduler), not for runtime dispatch.
//
// Phase order:
//   1. Input       - Poll events, read input state.
//                    Must run before anything reads input.
//
//   2. Physics     - Fixed timestep. Gravity, friction, collision detection
//                    and response, movement, ground detection.
//                    Runs 0-N times per frame inside the accumulator loop.
//
//   3. GameLogic   - Gameplay rules that respond to physics results.
//                    Health, scoring, state machines, AI decisions.
//                    Runs once per frame, after all physics steps.
//
//   4. LateUpdate  - Post-logic cleanup. Camera follow, animation blending,
//                    transform hierarchy propagation.
//
//   5. Render      - Read positions, submit draw calls. Must be last.

enum class SystemPhase
{
	Input,
	Physics,
	GameLogic,
	LateUpdate,
	Render
};
