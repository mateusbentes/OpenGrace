#pragma once
// The scene module's data types: entities, spatial partitioning, queries and
// level of detail.
//
// The documented architecture (docs/grace-architecture.md section 3.8)
// requires: data-driven entities, spatial partitioning selectable among
// flat, grid, octree and quadtree, scene queries, and level of detail. This
// header carries the data; the Scene class in scene.hpp carries the
// operations. Nothing here names a graphics API, a physics SDK or a
// scripting language: a scene is plain data, so the serialisation strategy
// (decision 36) can write it into the OWD container.
#include <cstdint>
#include <string>
#include <vector>
#include "grace/core/math/vec3.hpp"
namespace grace {
namespace scene {
/// How the scene partitions its entities. The documented architecture
/// (section 3.8) makes the choice explicit: "spatial partitioning selectable
/// among flat, grid, octree, and quadtree". The choice is a property of the
/// scene, not a template parameter, so the same data can be re-partitioned
/// without rebuilding the entities.
enum class Partitioning : std::uint8_t
{
    Flat = 0,     ///< no partitioning: every query walks every entity
    Grid = 1,     ///< uniform axis-aligned grid, 2D cells (x, z)
    Octree = 2,   ///< octree over the world's AABB
    Quadtree = 3, ///< quadtree over the world's AABB, projected on the
                  ///< ground plane (x, z); the y axis is ignored
};
/// A level of detail: at most this world-space distance the entity is
/// rendered at its full detail, and beyond each successive LOD the
/// mesh's meshIndex is used instead. The documented architecture says
/// "level of detail" without a formula; the distances are the data, and
/// the engine's own convention decides the interpolation.
struct LodStep final
{
    float distance = 0.0f;   ///< world-space distance from the query point
    std::uint32_t meshIndex = 0; ///< the mesh to use at this distance
};
/// A data-driven entity: a name, a position, a scale, a mesh index, a
/// material index and a list of LOD steps. Every property is data; the
/// engine owns the behaviour, and the serialisation strategy (decision 36)
/// writes exactly this structure into the container. A "data-driven entity"
/// is a row of this vector, not a C++ object with virtual methods.
struct EntityDesc final
{
    std::string name;          ///< unique, non-empty
    core::vec3 position{0.0f, 0.0f, 0.0f};
    core::vec3 scale{1.0f, 1.0f, 1.0f};
    std::uint32_t meshIndex = 0;     ///< mesh to render at full detail
    std::uint32_t materialIndex = 0; ///< material index
    std::vector<LodStep> lods; ///< ascending distance; empty means no LOD
    /// Message-string style validation: empty name invalid, a duplicate
    /// name invalid when the scene is checked, an LOD list with a
    /// non-ascending distance invalid, a LOD distance that is not strictly
    /// greater than the previous one invalid. Zero scale is invalid.
    bool validate(std::string& message) const;
};
/// A query: a world-space AABB, filled by the scene with the entities
/// whose position falls inside it. The query walks the partitioning, so
/// its cost is the cells it touches, not the whole scene.
struct QueryResult
{
    std::vector<std::uint32_t> entityIndices;
};
/// A scene: the entities, the partitioning and the queries over it.
/// The data is plain; the operations are deterministic. The same
/// entities and the same partitioning always produce the same query
/// results, in the same order.
struct SceneStats final
{
    std::uint64_t entityCount = 0;
    std::uint64_t nodeCount = 0;  ///< partitioning nodes (flat: 0;
                                  ///< grid cells; octree/quadtree nodes)
    std::uint64_t queryCount = 0; ///< queries performed since the last reset
};
}  // namespace scene
}  // namespace grace
