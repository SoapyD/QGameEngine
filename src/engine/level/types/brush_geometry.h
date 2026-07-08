#pragma once

#include <glm/glm.hpp>

#include <string>
#include <vector>

// Output types for buildBrushGeometry (declared in ../brush_geometry.h). A brush
// resolved from its face half-spaces into explicit geometry, in ENGINE space.

namespace qmap
{
    // One brush face as a convex polygon, wound CCW as seen from OUTSIDE the brush
    // (GL_CCW front + back-face cull), with an outward normal.
    struct BrushFacePolygon
    {
        std::vector<glm::vec3> vertices;
        glm::vec3   normal{0.0f};
        std::string texture;
    };

    // Per-face polygons (for render surfaces) + the full set of corner vertices
    // (for a convex-hull collider).
    struct BrushGeometry
    {
        std::vector<BrushFacePolygon> faces;
        std::vector<glm::vec3>        vertices;
    };
}
