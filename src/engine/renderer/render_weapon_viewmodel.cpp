#include "engine/renderer/weapon_viewmodel.h"

#include "engine/renderer/mesh.h"
#include "engine/ecs/components.h"   // WeaponInventory, PlayerInput, TagPlayer

#include <glad/glad.h>
#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <cmath>

namespace
{
    // ─── Tunables (all in view space; -Z is "into the screen") ───────
    constexpr glm::vec3 kBaseOffset{ 0.30f, -0.27f, -0.55f }; // right, down, forward
    constexpr float kScale       = 0.90f;
    constexpr float kYawDeg      = 8.0f;  // angle the gun slightly across screen
    constexpr float kPitchDeg    = 2.0f;
    constexpr float kSwitchTime  = 0.35f; // seconds for the raise animation
    constexpr float kSwitchDrop  = 0.40f; // how far the gun drops during a switch
    constexpr float kRecoilBack  = 0.06f; // kick toward the camera on fire
    constexpr float kRecoilPitch = 9.0f;  // muzzle-up kick (degrees) on fire

    void setMat4(unsigned int prog, const char* name, const glm::mat4& m)
    {
        glUniformMatrix4fv(glGetUniformLocation(prog, name), 1, GL_FALSE, &m[0][0]);
    }
    void setVec3(unsigned int prog, const char* name, const glm::vec3& v)
    {
        glUniform3fv(glGetUniformLocation(prog, name), 1, &v[0]);
    }
}

void renderWeaponViewModel(WeaponViewModel& vm, entt::registry& registry,
                           const Camera& camera, float aspectRatio,
                           unsigned int litShader, float frameTime)
{
    // Find the player's active, owned weapon.
    auto view = registry.view<WeaponInventory, PlayerInput, TagPlayer>();
    for (auto [entity, inv, input] : view.each())
    {
        int slot = inv.currentWeapon;
        if (slot < 0 || slot >= 7 || !inv.owned[slot] || !vm.meshes[slot])
            return;

        // ─── Advance animation state ─────────────────────────────
        bool moving = glm::length(input.wishDir) > 0.1f;
        vm.bobPhase += frameTime * (moving ? 9.0f : 2.5f);
        float bobAmp = moving ? 0.010f : 0.0035f;
        float bobX = std::sin(vm.bobPhase) * bobAmp;
        float bobY = -std::fabs(std::sin(vm.bobPhase)) * bobAmp;

        if (slot != vm.lastWeapon)
        {
            vm.switchTimer = kSwitchTime;
            vm.lastWeapon = slot;
        }
        vm.switchTimer = std::max(0.0f, vm.switchTimer - frameTime);
        float switchDrop = (vm.switchTimer / kSwitchTime) * kSwitchDrop;

        const Weapon& w = inv.weapons[slot];
        float recoil = (w.fireRate > 0.0f)
            ? std::clamp(w.cooldownRemaining / w.fireRate, 0.0f, 1.0f) : 0.0f;

        // ─── Build the view-space model matrix ───────────────────
        glm::vec3 p = kBaseOffset;
        p.x += bobX;
        p.y += bobY - switchDrop;
        p.z += recoil * kRecoilBack;   // kick toward the camera

        glm::mat4 model(1.0f);
        model = glm::translate(model, p);
        model = glm::rotate(model, glm::radians(kYawDeg), glm::vec3(0, 1, 0));
        model = glm::rotate(model, glm::radians(kPitchDeg + recoil * kRecoilPitch),
                            glm::vec3(1, 0, 0));
        model = glm::scale(model, glm::vec3(kScale));

        // ─── Uniforms: view = identity (model is already camera-space) ──
        glUseProgram(litShader);
        setMat4(litShader, "model", model);
        setMat4(litShader, "view", glm::mat4(1.0f));
        setMat4(litShader, "projection", camera.getProjectionMatrix(aspectRatio));
        setVec3(litShader, "viewPos", glm::vec3(0.0f));
        glUniform1f(glGetUniformLocation(litShader, "shininess"), 20.0f);

        // Fixed viewmodel lighting so the gun looks the same regardless of the
        // room. (renderSystem re-sets all of these next frame.)
        glUniform1i(glGetUniformLocation(litShader, "hasDirLight"), 1);
        setVec3(litShader, "dirLightDir", glm::normalize(glm::vec3(-0.5f, -0.7f, -0.6f)));
        setVec3(litShader, "dirLightColor", glm::vec3(1.0f));
        glUniform1f(glGetUniformLocation(litShader, "dirLightAmbient"), 0.45f);
        glUniform1i(glGetUniformLocation(litShader, "numPointLights"), 0);
        glUniform4f(glGetUniformLocation(litShader, "colorOverride"), 0, 0, 0, 0);

        // Flat-albedo path: shaded per-weapon colour, no texture.
        glUniform1i(glGetUniformLocation(litShader, "useAlbedo"), 1);
        setVec3(litShader, "albedoColor", vm.colors[slot]);

        // Draw over the world so the gun is never clipped by geometry, but keep
        // depth testing so it self-occludes correctly.
        glClear(GL_DEPTH_BUFFER_BIT);
        glBindVertexArray(vm.meshes[slot]->getVAO());
        glDrawElements(GL_TRIANGLES, vm.meshes[slot]->getIndexCount(), GL_UNSIGNED_INT, 0);

        // Reset so the next frame's world pass isn't tinted (renderSystem never
        // touches useAlbedo).
        glUniform1i(glGetUniformLocation(litShader, "useAlbedo"), 0);
        return;
    }
}
