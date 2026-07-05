#pragma once

#include <memory>
#include <string>
#include <utility>
#include <vector>

struct Sector;
class Mesh;

// Build one render Mesh per distinct texture name across a sector's surfaces.
// Mirrors build_sector_meshes' vertex/UV layout but groups by Surface::textureName,
// so a multi-texture map (e.g. orange walls + grey floor) renders correctly — the
// single-sector render path can only bind one texture. Needs a live GL context;
// the returned meshes must be kept alive for as long as they are drawn.
std::vector<std::pair<std::string, std::unique_ptr<Mesh>>>
buildTexturedMeshes(const Sector& sector);
