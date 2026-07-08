#include "engine/level/brush_geometry.h"

#include "engine/level/map_transform.h"

#include <algorithm>
#include <cmath>

namespace
{
    constexpr float kEps = 1e-3f;   // engine-space tolerance (units are small)

    // A face plane: interior of the brush is dot(n, x) <= d.
    struct Plane { glm::vec3 n; float d; };

    // Intersection point of three planes; false if (near-)parallel / singular.
    bool intersect3(const Plane& a, const Plane& b, const Plane& c, glm::vec3& out)
    {
        glm::vec3 bc = glm::cross(b.n, c.n);
        float denom = glm::dot(a.n, bc);
        if (std::fabs(denom) < 1e-6f) return false;
        out = (a.d * bc
             + b.d * glm::cross(c.n, a.n)
             + c.d * glm::cross(a.n, b.n)) / denom;
        return true;
    }
}

namespace qmap
{
    BrushGeometry buildBrushGeometry(const MapBrush& brush)
    {
        BrushGeometry geo;
        const size_t nf = brush.faces.size();
        if (nf < 4) return geo;   // a bounded solid needs ≥ 4 faces

        // Face planes in engine space. Quake/TrenchBroom writes each face's three
        // points CLOCKWISE as seen from the front, so cross(p2-p0, p1-p0) is the
        // OUTWARD normal and the brush interior is dot(n, x) <= d. (Deriving "inside"
        // from the average of the face points fails: TrenchBroom emits each face as a
        // tiny plane-defining triangle, so those points cluster at a couple of corners
        // rather than spanning the brush, and their average can fall outside it.)
        // The map→engine transform is a rotation (det +1), so the winding — hence the
        // outward direction — is preserved.
        std::vector<Plane> planes(nf);
        for (size_t i = 0; i < nf; ++i)
        {
            glm::vec3 p0 = mapPointToEngine(brush.faces[i].points[0]);
            glm::vec3 p1 = mapPointToEngine(brush.faces[i].points[1]);
            glm::vec3 p2 = mapPointToEngine(brush.faces[i].points[2]);
            glm::vec3 n  = glm::normalize(glm::cross(p2 - p0, p1 - p0));
            planes[i] = { n, glm::dot(n, p0) };
        }

        auto inside = [&](const glm::vec3& v)
        {
            for (const auto& pl : planes)
                if (glm::dot(pl.n, v) > pl.d + kEps) return false;
            return true;
        };

        // Corner vertices = triple-plane intersections that lie inside every
        // half-space. Record each on the (up to 3) faces it sits on.
        std::vector<std::vector<glm::vec3>> faceVerts(nf);
        for (size_t i = 0; i < nf; ++i)
          for (size_t j = i + 1; j < nf; ++j)
            for (size_t k = j + 1; k < nf; ++k)
            {
                glm::vec3 v;
                if (!intersect3(planes[i], planes[j], planes[k], v)) continue;
                if (!inside(v)) continue;
                geo.vertices.push_back(v);
                for (size_t f : { i, j, k })
                    if (std::fabs(glm::dot(planes[f].n, v) - planes[f].d) < kEps)
                        faceVerts[f].push_back(v);
            }

        if (geo.vertices.size() < 4) { geo = {}; return geo; }

        glm::vec3 centroid(0.0f);
        for (const auto& v : geo.vertices) centroid += v;
        centroid /= static_cast<float>(geo.vertices.size());

        // Each face: dedup its corner points, orient the normal outward (insurance
        // against a mis-wound source face), then sort the points CCW-from-outside.
        for (size_t i = 0; i < nf; ++i)
        {
            std::vector<glm::vec3> pts;
            for (const auto& v : faceVerts[i])
            {
                bool dup = false;
                for (const auto& q : pts) if (glm::length(v - q) < kEps) { dup = true; break; }
                if (!dup) pts.push_back(v);
            }
            if (pts.size() < 3) continue;   // sliver / degenerate face

            glm::vec3 fc(0.0f);
            for (const auto& v : pts) fc += v;
            fc /= static_cast<float>(pts.size());

            glm::vec3 n = planes[i].n;
            if (glm::dot(n, fc - centroid) < 0.0f) n = -n;   // point away from the brush

            // In-plane basis (u, w) with u×w = n, so increasing atan2(w,u) is CCW
            // when viewed from the +n (outside) side.
            glm::vec3 ref = std::fabs(n.x) < 0.9f ? glm::vec3(1, 0, 0) : glm::vec3(0, 1, 0);
            glm::vec3 u = glm::normalize(glm::cross(n, ref));
            glm::vec3 w = glm::cross(n, u);
            std::sort(pts.begin(), pts.end(), [&](const glm::vec3& a, const glm::vec3& b)
            {
                return std::atan2(glm::dot(a - fc, w), glm::dot(a - fc, u))
                     < std::atan2(glm::dot(b - fc, w), glm::dot(b - fc, u));
            });

            BrushFacePolygon poly;
            poly.vertices = std::move(pts);
            poly.normal   = n;
            poly.texture  = brush.faces[i].texture;
            geo.faces.push_back(std::move(poly));
        }

        return geo;
    }
}
