#include "engine/renderer/weapon_viewmodel.h"

#include "engine/renderer/gun_mesh.h"          // weaponColor
#include "engine/core/resource_manager.h"

#include <string>

WeaponViewModel createWeaponViewModel(const ResourceManager& resources)
{
    WeaponViewModel vm;
    for (int i = 0; i < 7; ++i)
    {
        vm.meshes[i] = resources.getMesh("gun_" + std::to_string(i));
        vm.colors[i] = weaponColor(static_cast<WeaponType>(i));
    }
    return vm;
}
