#include "engine/ecs/systems/enemy/ai_support.h"

#include "engine/ecs/components.h"
#include "engine/ecs/systems/combat/combat_internal.h"   // fireProjectile
#include "engine/audio/queue_sound.h"

// Fire one dodgeable Enemy-faction bolt from `eye` toward `dir`. Reuses the combat
// projectile path — a throwaway Weapon carries the ranged stats. No splash, so it
// can never friendly-fire other enemies.
void aiFireEnemyBolt(entt::registry& reg, entt::entity self, glm::vec3 eye, glm::vec3 dir,
                     const RangedAttack& r, const CombatResources& res)
{
    Weapon w{};
    w.type            = WeaponType::Nailgun;      // borrows the nailgun fire sound
    w.fireMode        = FireMode::Projectile;
    w.damage          = r.damage;
    w.projectileSpeed = r.projectileSpeed;
    w.splashRadius    = 0.0f;
    fireProjectile(reg, self, w, eye, dir, res);
    queueSoundAt(reg, "weapon.nailgun", eye);
}
