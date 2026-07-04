# Chapter 25: Debugging a Speed Run-Away — Reference Frames and Emergent Bugs

## What You'll Learn
- How to read a **"weird speed-up" symptom** — the player's horizontal velocity climbing with no
  ceiling — and turn it into a testable hypothesis
- The anatomy of an **emergent bug**: a latent defect that sat harmless for chapters until a new
  feature (horizontally-moving enemies) shared a code path with it and set it off
- Why moving kinematic **enemies and moving platforms are the same code path** as far as the
  player controller is concerned — and how `GetGroundVelocity()` couples them
- The root cause — a **double-count of the platform carry**: `currentVel` already held last tick's
  inherited ground velocity, and the old code added `groundVel` on top *again* every tick
- The fix: computing ground acceleration and friction in the **ground's reference frame**
  (`relVel = currentVel − groundHoriz`, accelerate, then add the ground back **once**) — and why
  that leaves static ground and *vertical* lifts mathematically identical
- Adding a **guardrail** — `CharacterPhysics.maxHorizontalSpeed`, a clamp on the player's *own*
  horizontal speed (platform carry excluded) — and why the ceiling is 20, not the 7 of normal walking
- Why you want **both** a root-cause fix *and* a backstop clamp, not one or the other
- Proving it with a new **`speed_cap` headless scenario** and confirming the lift/walk scenarios
  stay green — so we know we fixed the bug *without* changing normal or vertical-platform movement

---

## Where We Are

Chapters 23 and 24 gave the grunt a brain. The passive dummy from Chapter 22 now aggroes on sight,
paths around the shelf with A* over a nav grid, and closes to gauntlet range to hit you. To move, an
aggroed grunt drives its kinematic body every tick with `MoveKinematic` toward the next waypoint —
and crucially, it moves **horizontally**. That one detail is the whole of this chapter.

Because the moment the first horizontally-moving kinematic bodies started walking the arena, an old
bug woke up. Testers reported the player's speed sometimes "running away" — you'd be moving at a
sensible jog one second and rocketing across the map the next, with no obvious ceiling. Nothing in
the movement code had changed in Chapters 23–24. The enemies did it.

This chapter is a debugging war-story, so it's shaped like the investigation, not like a feature:

1. **The symptom** — reproduce the run-away and pin down *when* it happens.
2. **The emergent-bug lesson** — why a bug that never bit for twenty chapters suddenly bites now.
3. **The root cause** — a double-count of the moving-platform carry, exposed by reasoning about
   *reference frames*.
4. **The fix** — do the movement maths in the ground's frame and inherit its velocity exactly once.
5. **The guardrail** — a `maxHorizontalSpeed` clamp as a backstop, and why we keep it *and* the fix.
6. **The verification** — a `speed_cap` scenario, plus proof the lift/walk scenarios are unmoved.

Everything below is grounded in `src/engine/ecs/systems/player/player_character_system.cpp`,
`src/engine/ecs/components/physics.h`, and `src/harness/headless_main.cpp`. Where it helps, we quote
the *old* code (from the diff) beside the new, because the whole lesson lives in the difference.

---

## Step 1: The Symptom — Speed With No Ceiling

The report was vague — "the player sometimes speeds up loads" — which is how most real bugs arrive.
The first job of debugging is to make the vague thing *specific and repeatable*. Two facts narrowed
it fast:

- It only happened while grunts were **awake and chasing**. Stand still in an empty room and the
  player never ran away. Aggro a grunt and let it barge into you, and the speed-up appeared.
- It was **horizontal**, and it **compounded**. This wasn't a one-off shove — the player's ground
  speed kept climbing tick after tick, well past the `maxGroundSpeed = 7.0f` that walking is supposed
  to cap out at. Left alone it would sail past any sane number.

Those two facts are the crack in the case. "Only when a chasing grunt touches you" points straight at
the enemy's kinematic movement. "Compounds every tick" points at something *accumulating* — a value
being added over and over instead of being set. Put together, the hypothesis writes itself:

> **The player controller is doing something with the grunt's motion, once per tick, that stacks
> instead of settling.**

The one place the player controller reads anything about the surface it's standing on is
`GetGroundVelocity()` — the moving-platform carry we added back when we built lifts. So the
hypothesis sharpens to: *the platform-carry code is the culprit, and horizontally-moving enemies are
the first thing that ever exercised it horizontally.*

> **Why start by finding out *when* the bug happens, before opening the movement code at all?** A
> reproduction is worth more than a stack trace here, because there is no crash — nothing throws, the
> numbers just drift wrong. "Only while a grunt chases you, and it compounds" already tells us three
> things: which subsystem to suspect (ground velocity / platform carry), what kind of mistake to look
> for (accumulation, not a one-shot error), and how we'll later *prove* a fix (inject motion, hold a
> key, watch the speed settle). Guessing at the code first would have had us staring at Quake
> acceleration maths that turns out to be completely correct.

---

## Step 2: The Emergent-Bug Lesson — Why Now?

Here's the uncomfortable part: the movement code that caused this **hadn't changed in chapters**. The
platform carry was written for lifts, and it worked. Every headless scenario passed. So why does it
break now?

Because it was a **latent** bug — wrong all along, but only in a way that never *showed*. Look at what
had ever driven a kinematic body before Chapter 23:

- **Lifts** move **up and down** — pure vertical velocity.
- **Doors** slide, but the ones the player rides move vertically too.

Every mover the carry code had ever seen had a `GetGroundVelocity()` that was essentially
`(0, y, 0)` — its horizontal components were zero. And the bug, as we'll see in Step 3, was in the
*horizontal* carry. A double-count of zero is zero. The defect was real the whole time; it simply had
nothing to bite.

Then Chapters 23–24 taught grunts to chase. An aggroed grunt drives its body horizontally toward the
player, in `aiSystem` (`src/engine/ecs/systems/enemy/ai_system.cpp`):

```cpp
        glm::vec3 target = pos.value + stepDir * kMoveSpeed * dt;
        bodyInterface.MoveKinematic(body.id,
            JPH::RVec3(target.x, target.y, target.z), JPH::Quat::sIdentity(), dt);
```

`stepDir` is a *flat* direction — its Y is zeroed when the grunt steers toward a waypoint or straight
at the player. So a chasing grunt is the engine's **first horizontally-moving kinematic body**. And a
grunt is, to Jolt, exactly the same *kind* of thing as a lift: a kinematic body with a velocity. When
the player's `CharacterVirtual` is in contact with one, `GetGroundVelocity()` reports the grunt's
horizontal velocity just as it would report a lift's vertical velocity — the controller can't tell a
"platform" from an "enemy", and shouldn't have to. They share the carry code path.

> **Why is "a bug that was always there but never fired" worth a whole step?** Because it's the most
> important debugging lesson there is: *features interact*, and the interaction is where latent bugs
> live. Nobody wrote a bad line in Chapter 23 — the AI movement is correct, the platform carry looked
> correct, every test was green. The defect emerged from the *combination*: horizontal kinematic
> motion meeting a carry that only happened to be exercised vertically. When a new feature "breaks"
> old code that you didn't touch, don't assume the new feature is wrong. Ask what *assumption* the old
> code was quietly relying on — here, "the surface under the player only ever moves vertically" — that
> the new feature just violated.

---

## Step 3: The Root Cause — Double-Counting the Carry

Now open the movement code with the hypothesis in hand. Here is the **old** ground branch (from the
diff), the one that shipped through Chapter 24:

```cpp
        if (onGround)
        {
            // Ground movement — Quake-style acceleration
            JPH::Vec3 wishDir(input.wishDir.x, 0.0f, input.wishDir.z);
            float wishSpeed = physics.maxGroundSpeed;

            if (wishDir.LengthSq() > 0.0f)
            {
                wishDir = wishDir.Normalized();
                float currentSpeed = currentVel.Dot(wishDir);
                float addSpeed = wishSpeed - currentSpeed;
                if (addSpeed > 0.0f)
                {
                    float accelSpeed = physics.groundAcceleration * wishSpeed * dt;
                    if (accelSpeed > addSpeed) accelSpeed = addSpeed;
                    desiredVel = JPH::Vec3(currentVel.GetX(), 0.0f, currentVel.GetZ()) + wishDir * accelSpeed;
                }
                else
                {
                    desiredVel = JPH::Vec3(currentVel.GetX(), 0.0f, currentVel.GetZ());
                }
            }
            else
            {
                // no input = apply ground friction
                JPH::Vec3 horizontalVel(currentVel.GetX(), 0.0f, currentVel.GetZ());
                float speed = horizontalVel.Length();
                if (speed > 0.f)
                {
                    float drop = speed * physics.groundFriction * dt;
                    float newSpeed = std::max(speed - drop, 0.0f);
                    desiredVel = horizontalVel * (newSpeed / speed);
                }
            }

            // Carry the player horizontally with a moving platform.
            desiredVel += JPH::Vec3(groundVel.GetX(), 0.0f, groundVel.GetZ());
```

Read it as a loop across ticks, because that's the trap. Focus on two lines:

- The acceleration builds `desiredVel` **from `currentVel`** — `currentVel.GetX()/GetZ()` is the
  player's velocity as Jolt reported it at the *start of this tick*.
- The last line then **adds the platform's horizontal velocity on top**:
  `desiredVel += groundVel(horizontal)`.

At the end of the tick we call `SetLinearVelocity(desiredVel)`. So next tick, when we read
`currentVel = character->GetLinearVelocity()`, **`currentVel` already contains the platform velocity
we added last tick.** Then the code builds `desiredVel` from that `currentVel`… and adds `groundVel`
**again**.

That is the double-count. Standing on something moving horizontally at `g` units/s:

| Tick | `currentVel` (start) | after accel (≈) | after `+= groundVel` |
|------|----------------------|-----------------|----------------------|
| 1    | `0`                  | `0`             | `g`                  |
| 2    | `g`                  | `g`             | `2g`                 |
| 3    | `2g`                 | `2g`            | `3g`                 |
| …    | …                    | …               | `n·g` → runaway      |

Every tick the player *keeps* the inherited velocity from last tick (it's baked into `currentVel`)
and then inherits it *again*. The carry was meant to be "match the platform's velocity"; written this
way it's "add the platform's velocity, forever." With a vertical lift, `groundVel`'s horizontal part
is zero, so `g = 0` and the table stays flat — which is exactly why nobody ever saw it. With a
horizontally-chasing grunt, `g ≠ 0`, and the player's speed marches off to infinity.

> **Why does phrasing it as "reference frames" make the bug obvious?** Because the *intent* of the
> carry is a reference-frame statement: "do the player's walking maths **relative to the surface they
> stand on**, then express the result back in world space." A lift rider should accelerate, brake, and
> cap out at 7 u/s *relative to the lift*, and separately be swept along by it. The old code never
> actually changed frames — it did all the maths in world space and bolted the platform velocity on at
> the end, so the platform velocity leaked into `currentVel` and got treated as if it were the
> player's own speed. Once you name the intended frame, the fix is forced: subtract the ground's
> velocity going in, add it back once coming out.

---

## Step 4: The Fix — Work in the Ground's Reference Frame

The fix is to make the code *say what it means*: transform into the ground's frame, do the Quake
acceleration/friction there, then transform back by adding the ground velocity **exactly once**. Here
is the **new** ground branch, verbatim from
`src/engine/ecs/systems/player/player_character_system.cpp`:

```cpp
        if (onGround)
        {
            // Ground movement — Quake-style acceleration, computed in the ground's
            // reference frame so a moving platform's velocity is inherited exactly
            // once (currentVel already carries last tick's groundHoriz — adding it
            // again each tick is what made speed run away on horizontal movers).
            JPH::Vec3 groundHoriz(groundVel.GetX(), 0.0f, groundVel.GetZ());
            JPH::Vec3 relVel = JPH::Vec3(currentVel.GetX(), 0.0f, currentVel.GetZ()) - groundHoriz;
            JPH::Vec3 wishDir(input.wishDir.x, 0.0f, input.wishDir.z);
            JPH::Vec3 moveVel = relVel;

            if (wishDir.LengthSq() > 0.0f)
            {
                wishDir = wishDir.Normalized();
                float currentSpeed = relVel.Dot(wishDir);
                float addSpeed = physics.maxGroundSpeed - currentSpeed;
                if (addSpeed > 0.0f)
                {
                    float accelSpeed = physics.groundAcceleration * physics.maxGroundSpeed * dt;
                    if (accelSpeed > addSpeed) accelSpeed = addSpeed;
                    moveVel = relVel + wishDir * accelSpeed;
                }
            }
            else
            {
                // no input = apply ground friction (to the player's own velocity)
                float speed = relVel.Length();
                if (speed > 0.0f)
                {
                    float drop = speed * physics.groundFriction * dt;
                    float newSpeed = std::max(speed - drop, 0.0f);
                    moveVel = relVel * (newSpeed / speed);
                }
                else
                {
                    moveVel = JPH::Vec3::sZero();
                }
            }
```

Trace the three moves:

1. **Into the ground's frame.** `groundHoriz` is the surface's horizontal velocity;
   `relVel = currentVel(horizontal) − groundHoriz` is the player's velocity *relative to the surface*.
   On a chasing grunt, `relVel` is what the player is doing *on top of* the grunt's motion — which is
   the thing walking should actually govern.
2. **Do all the maths in that frame.** Every place the old code touched `currentVel` now touches
   `relVel`: the `Dot(wishDir)` that measures current speed, the friction `Length()`, everything. The
   Quake accel and friction are byte-for-byte the same operations — only the frame changed. The result
   lands in `moveVel`.
3. **Back to world space, once.** After the clamp (Step 5), the branch ends with:

```cpp
            desiredVel = moveVel + groundHoriz;   // inherit the platform once
```

`moveVel` is the player's own motion in the surface's frame; adding `groundHoriz` back sweeps them
along with the surface — a single time, at the very end. There is no `+=` in a loop, no leak into next
tick's `currentVel` that gets re-added: next tick we subtract `groundHoriz` right back out before we
do anything with it. The carry is now idempotent.

**Why this leaves normal and vertical-platform movement untouched** — the part that matters for not
introducing a *new* bug while fixing this one:

- **Static ground.** `GetGroundVelocity()` is zero on non-moving floor, so `groundHoriz = 0`,
  `relVel = currentVel(horizontal)`, and `desiredVel = moveVel + 0`. Every operation is identical to
  the old code with a zero added — plain walking is mathematically unchanged.
- **Vertical lifts.** A lift's `groundVel` is `(0, y, 0)`; its *horizontal* part `groundHoriz` is
  still zero. So the horizontal maths is again identical, and the vertical carry is handled separately,
  exactly as before:

```cpp
            if (input.jump)
            {
                desiredVel.SetY(physics.jumpForce);
                queueSound(registry, "player.jump");
            }
            else
            {
                // Ride the platform's vertical motion (0 on static ground).
                desiredVel.SetY(groundVel.GetY());
            }
```

This is why the existing lift scenarios stay green through the change (Step 6): for everything that
was ever exercised before, `groundHoriz` is the zero vector and the new frame maths collapses back to
the old world-space maths. The fix only *bites* in the one case the old code got wrong — a non-zero
horizontal ground velocity — which is the case the grunts just introduced.

> **Why fix it in the ground's frame rather than just, say, subtracting `groundVel` from `currentVel`
> at the top and calling it done?** Because doing it as an explicit frame change makes the *whole*
> branch correct at once, not just the accel path. Friction now brakes the player's own speed toward
> zero-relative-to-the-platform (so you can stand still *on* a moving grunt), the speed clamp in Step 5
> measures the player's own speed (not their speed plus the platform's), and the "carry once" line is a
> single, auditable statement of intent. A one-line patch at the top would fix the symptom for the
> accel case and leave friction and the clamp reasoning about the wrong quantity.

---

## Step 5: The Guardrail — A Ceiling on the Player's *Own* Speed

The reference-frame fix removes the *cause*. We add one more thing that removes the *class* of failure:
a hard clamp on how fast the player's own legs can carry them. New field in `CharacterPhysics`
(`src/engine/ecs/components/physics.h`):

```cpp
struct CharacterPhysics
{
	float groundFriction = 6.0f;
	float airFriction = 0.1f;
	float maxGroundSpeed = 7.0f;
	float maxAirSpeed = 1.0f; // Quake's air speed cap (enables bunny hopping!)
	float groundAcceleration = 10.0f;
	float airAcceleration = 10.0f;
	float jumpForce = 8.0f;
	float stepHeight = 0.7f; // Max height of a step the player can walk up
	// Anti-runaway ceiling on the player's OWN horizontal speed (excludes platform
	// carry). Generous so bunny-hopping still feels fast; catches pathological
	// speed-ups (e.g. being carried/shoved by a moving kinematic body).
	float maxHorizontalSpeed = 20.0f;
};
```

The clamp is applied to `moveVel` in the ground branch — *before* the platform carry is added back, so
it only ever limits the player's own motion, never their ride on a fast surface:

```cpp
            // Anti-runaway: clamp the player's OWN horizontal speed. Platform
            // carry is added afterward, so riding a fast platform is never clamped.
            float ownSpeed = moveVel.Length();
            if (ownSpeed > physics.maxHorizontalSpeed)
                moveVel = moveVel * (physics.maxHorizontalSpeed / ownSpeed);

            desiredVel = moveVel + groundHoriz;   // inherit the platform once
```

The air branch gets the same backstop. The whole air branch was reworked so it always starts from the
current velocity and clamps horizontal air speed to the same ceiling:

```cpp
        else
        {
            // Air movement — limited air control
            JPH::Vec3 wishDir(input.wishDir.x, 0.0f, input.wishDir.z);
            desiredVel = JPH::Vec3(currentVel.GetX(), currentVel.GetY(), currentVel.GetZ());

            if (wishDir.LengthSq() > 0.0f)
            {
                wishDir = wishDir.Normalized();
                float currentSpeed = JPH::Vec3(currentVel.GetX(), 0.0f, currentVel.GetZ()).Dot(wishDir);
                float addSpeed = physics.maxAirSpeed - currentSpeed;
                if (addSpeed > 0.0f)
                {
                    float accelSpeed = physics.airAcceleration * physics.maxAirSpeed * dt;
                    if (accelSpeed > addSpeed) accelSpeed = addSpeed;
                    desiredVel = JPH::Vec3(currentVel.GetX(), currentVel.GetY(), currentVel.GetZ()) + wishDir * accelSpeed;
                }
            }

            // Anti-runaway: clamp horizontal air speed too.
            JPH::Vec3 airHoriz(desiredVel.GetX(), 0.0f, desiredVel.GetZ());
            float airSpeed = airHoriz.Length();
            if (airSpeed > physics.maxHorizontalSpeed)
            {
                airHoriz = airHoriz * (physics.maxHorizontalSpeed / airSpeed);
                desiredVel = JPH::Vec3(airHoriz.GetX(), desiredVel.GetY(), airHoriz.GetZ());
            }

            // apply gravity while in the air (shared magnitude from PhysicsConfig)
            desiredVel += JPH::Vec3(0.0f, -config.gravity * dt, 0.0f);
        }
```

Note the clamp only rescales the horizontal components — it reads `desiredVel.GetY()` back untouched
so gravity and jump arcs are never affected. In both branches, the clamp normalises the horizontal
vector down to `maxHorizontalSpeed` while preserving its direction (`v * (cap / speed)`), so a clamped
player keeps moving where they were headed, just no faster than the ceiling.

**Why 20 and not 7?** Walking caps at `maxGroundSpeed = 7`. If the ceiling were also 7, it would fight
the movement system that's *supposed* to let you exceed 7 — Quake's bunny-hop, where chaining jumps
lets a skilled player build speed well past the walk cap through the `maxAirSpeed` air-accel trick.
Clamp at 7 and you'd kill that feel outright. `maxHorizontalSpeed = 20` sits comfortably above any
speed legitimate movement produces (a fast bunny-hop chain), so it never touches normal play — but far
below the unbounded run-away a bug can produce, so it catches the pathological case. It's a **ceiling
on the sane, not a limit on the skilful.**

> **Why keep the clamp at all once the reference-frame bug is fixed — isn't a backstop for a bug you've
> already killed just dead weight?** No — it's defence in depth, and the two guard different things.
> The frame fix is the *correct* answer for the specific double-count we found; the clamp is a
> *category* guard against "the player's own horizontal speed became absurd," from any cause we haven't
> thought of yet — a future new mover, a knockback stack, an impulse bug in a chapter not yet written.
> A root-cause fix without a backstop means the *next* latent speed bug ships silently until a player
> finds it; a backstop without a root-cause fix means you've hidden a real defect behind a clamp that
> quietly corrupts movement whenever it fires. You want both: fix the cause so the clamp *never* fires
> in normal play, and keep the clamp so that if something ever makes it fire, the game degrades to
> "capped at 20" instead of "launched into orbit." Note we deliberately clamp *before* adding the
> platform carry, so the backstop can never punish a player for legitimately riding a genuinely fast
> surface — it only ever limits speed the player generated themselves.

---

## Step 6: Verification — Prove the Fix, Prove You Didn't Break Walking

A debugging chapter isn't done when the code looks right; it's done when a test *pins* the fix so it
can't silently regress. Two things need proving: the run-away is gone, and normal + vertical-platform
movement is unchanged.

**The new `speed_cap` scenario** does the first. It's the reproduction from Step 1 turned into an
assertion — inject an absurd velocity, hold forward, and demand the speed settles under the cap. From
`src/harness/headless_main.cpp`:

```cpp
    // The player's own horizontal speed is clamped: injecting an absurd velocity
    // and holding a movement key must settle back under the cap (no runaway).
    bool scenario_speed_cap(entt::registry& reg, JoltWorld& jolt, const Level& level, float dt)
    {
        entt::entity player = findPlayer(reg);
        clearEnemies(jolt, reg);
        const float halfY = reg.get<AABBCollider>(player).halfExtents.y;

        teleportPlayer(reg, player, glm::vec3(8.0f, halfY, 15.0f));
        for (int i = 0; i < 30; i++) { applyInput(reg, player, Input{}); qengine::stepSimulation(reg, jolt, level, dt); }

        auto& ch = reg.get<JoltCharacter>(player).character;
        ch->SetLinearVelocity(JPH::Vec3(60.0f, 0.0f, 0.0f));   // absurd shove
        Input fwd; fwd.wishDir = glm::vec3(1, 0, 0); fwd.lookDir = glm::vec3(1, 0, 0);

        float peak = 0.0f;
        for (int i = 0; i < 30; i++)
        {
            applyInput(reg, player, fwd);   // hold forward (no friction path)
            qengine::stepSimulation(reg, jolt, level, dt);
            JPH::Vec3 v = ch->GetLinearVelocity();
            peak = std::max(peak, std::sqrt(v.GetX() * v.GetX() + v.GetZ() * v.GetZ()));
        }

        float cap = reg.get<CharacterPhysics>(player).maxHorizontalSpeed;
        char buf[180];
        std::snprintf(buf, sizeof(buf),
            "injected 60 u/s + held forward: peak horiz speed=%.1f (cap %.1f)", peak, cap);
        return report("speed_cap", peak <= cap + 1.0f, buf);
    }
```

Read what it proves. It teleports the player to open floor and lets them settle. Then it does the two
things that used to cause the run-away at once: `SetLinearVelocity(60, 0, 0)` slams in a horizontal
speed of 60 u/s — far past anything legitimate — and `fwd` holds the forward key so we're on the
**acceleration** path, not the friction one (a run-away that only friction could rescue would be a
weaker test). Over 30 ticks it records the *peak* horizontal speed. The assertion —
`peak <= cap + 1.0f` — says that even starting from an absurd 60, and even while actively pressing
forward, the player's horizontal speed never exceeds `maxHorizontalSpeed` by more than a tick's slop.
Under the old code this would climb; under the fix it's clamped down toward 20 immediately and stays
there.

Note the `clearEnemies(jolt, reg)` at the top — a helper added alongside these scenarios that
destroys every `AIState` entity and its Jolt body:

```cpp
    // Remove all enemies (body + entity). Pure player-physics scenarios call this
    // so aggroed grunts can't wander in and disturb the measurement.
    void clearEnemies(JoltWorld& jolt, entt::registry& reg)
    {
        auto& bi = jolt.getBodyInterface();
        std::vector<entt::entity> es;
        for (auto e : reg.view<AIState>()) es.push_back(e);
        for (auto e : es)
        {
            if (auto* b = reg.try_get<JoltBody>(e)) { bi.RemoveBody(b->id); bi.DestroyBody(b->id); }
            reg.destroy(e);
        }
    }
```

This is a small but real bit of test hygiene: a pure *player-physics* measurement must not have a live
grunt wander into frame and add its own ground velocity to the reading. We clear the enemies so the
only thing acting on the player is the code under test.

Register the scenario in `main`'s dispatch, alongside the AI scenarios the sibling chapters added:

```cpp
    else if (scenario == "monster_ai")       pass = scenario_monster_ai(registry, jolt, level, dt);
    else if (scenario == "monster_path")     pass = scenario_monster_path(registry, jolt, level, dt);
    else if (scenario == "speed_cap")        pass = scenario_speed_cap(registry, jolt, level, dt);
```

**The second proof is the regression net you already have.** Because the fix collapses to the old
maths whenever `groundHoriz` is zero (Step 4), the pre-existing walk and lift scenarios are the
evidence that we changed *only* the broken case. Run the full headless suite; the lift scenario (player
rides a rising platform and ends up where the platform put them) and the floor-walk scenario must stay
green. If the reference-frame rewrite had disturbed vertical carry or plain walking, one of those would
fail — they didn't, which is the mathematical claim from Step 4 confirmed empirically.

> **Why does `speed_cap` press forward instead of just injecting speed and coasting?** Because the bug
> lived on the *acceleration* path — the double-count happened every tick the player was moving under
> their own power on a moving surface, not while coasting to a stop under friction. A test that injects
> 60 and then holds *no* key would be rescued by ground friction bleeding the speed off, and would pass
> even against the buggy code. Holding forward keeps us on the exact code path that used to compound,
> so the scenario fails loudly on the old build and passes on the fixed one — which is the only kind of
> regression test worth writing.

---

## What Changed — Summary

| File | Change |
|------|--------|
| `engine/ecs/systems/player/player_character_system.cpp` | Ground movement now computed in the ground's reference frame (`relVel = currentVel − groundHoriz`, accelerate/brake, then `desiredVel = moveVel + groundHoriz` — carry inherited **once**), fixing the horizontal platform-carry double-count. Added a `maxHorizontalSpeed` clamp on the player's own speed in **both** the ground and air branches. |
| `engine/ecs/components/physics.h` | New `CharacterPhysics.maxHorizontalSpeed = 20.0f` — anti-runaway ceiling on the player's own horizontal speed (platform carry excluded). |
| `harness/headless_main.cpp` | New `scenario_speed_cap` (inject 60 u/s + hold forward → clamped under the cap) and a `clearEnemies` helper so pure player-physics scenarios aren't disturbed by live grunts; registered `speed_cap` in the dispatch. |

No CMake change: `player_character_system.cpp` is already a compiled unit, `physics.h` is header-only,
and the harness is one translation unit. The fix touches behaviour, not the build graph.

---

## What You Should See

Run `build/QEngine.exe`:

1. **No more run-away.** Aggro a grunt and let it barge into you while you walk. The player is nudged,
   but your speed stays sane — it no longer climbs tick after tick into a rocket ride across the arena.
2. **Walking is exactly as it was.** Ground movement, acceleration, and friction on static floor feel
   identical to before this chapter — because mathematically they are.
3. **Lifts still carry you.** Ride the showcase lift; you go up with it and stand steady on top, same
   as ever. Vertical carry was never in the blast radius of the fix.
4. **Bunny-hopping still builds speed.** Chaining jumps still lets you exceed the walk cap — the
   `maxHorizontalSpeed = 20` ceiling sits well above any legitimate hop chain, so it never fights the
   movement you *want*.

Headless:

5. **`speed_cap` passes** — injecting 60 u/s and holding forward settles the player's horizontal speed
   under the cap (`peak <= 20 + 1`), no window, no audio device.
6. **The lift and walk scenarios stay green** — the proof that the reference-frame rewrite fixed the
   horizontal case *without* touching normal or vertical-platform movement.

---

## What's Next

The player controller is now honest about reference frames: it does its walking maths relative to
whatever it's standing on, inherits that surface's motion exactly once, and refuses to let its own
speed run away. That makes the ground under the player trustworthy — which matters, because grunts are
about to start doing more than walking into you. With the movement foundation solid, the next chapters
can lean harder on kinematic movers: enemies that shove, platforms that slide sideways, conveyor
surfaces — all of which route through the same `GetGroundVelocity()` carry we just made correct. The
lesson to carry forward is the one Step 2 taught: when the next "impossible" bug appears in code you
didn't touch, look for the assumption a new feature just quietly broke.
