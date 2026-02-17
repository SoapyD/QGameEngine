#pragma once

#include <entt/entt.hpp>
#include "engine/renderer/camera.h"
#include "engine/renderer/shader.h"

void renderSystem(entt::registry& registry, const Camera& camera, float aspectRatio);