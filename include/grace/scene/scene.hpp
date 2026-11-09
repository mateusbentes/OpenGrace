#pragma once
// The scene module: data-driven entities, selectable spatial partitioning,
// scene queries and level of detail (docs/grace-architecture.md section
// 3.8).
//
// The scene is plain data, deliberately: the serialisation strategy
// (decision 36) writes exactly this structure into the OWD container, so
// the same data can be read back and the same entities can live in the
// engine's own structures rather than in the script host. Nothing in this
// file names a graphics API or a host platform.
#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>
#include "grace/core/math/vec3.hpp"
#include "grace/scene/scene_types.hpp"
namespace grace {
namespace scene {
/// An error: the scene refuses to hold an entity that does not validate,
/// or to partition into a world extent that does not. The message is the
/// scene's own words, and the failure is a build error, not a runtime
/// fallback.
class SceneError : public std::runtime_error
{
public:
    explicit SceneError(const std::string& message);
};
/// A scene instance: the entity list, the chosen partitioning and the
/// queries over it. The operations are deterministic.
class Scene
{
public:
    Scene();
    ~Scene();
    Scene(const Scene&) = delete;
    Scene& operator=(const Scene&) = delete;
    /// Adds an entity. The entity validates: a non-empty name, no
    /// duplicate name, a non-zero scale, LOD distances strictly
    /// ascending. Throws SceneError on any of these.
    void add(const EntityDesc& desc);
    /// The number of entities.
    std::size_t count() const;
    /// The entity by index, in the order the entities were added.
    const EntityDesc& entity(std::size_t index) const;
    /// The entities, in the order the entities were added. The
    /// serialisation strategy (decision 36) writes exactly this list.
    const std::vector<EntityDesc>& entities() const;
    /// Sets the partitioning. The choice is a property of the scene, and
    /// the scene can be re-partitioned at any time. Throws SceneError on
    /// a world extent that does not contain the entities, or a grid
    /// resolution that is zero.
    void setPartitioning(Partitioning kind, const core::vec3& worldMin,
                         const core::vec3& worldMax, std::uint32_t gridRes);
    /// The partitioning in use.
    Partitioning partitioning() const;
    /// Fills the result with the entities whose position falls inside the
    /// query's AABB. The query walks the partitioning, so its cost is the
    /// cells it touches, not the whole scene. The order is deterministic:
    /// ascending entity index.
    QueryResult query(const core::vec3& min, const core::vec3& max) const;
    /// The mesh index to use for an entity at a world-space distance from
    /// the query point. The LOD steps are the data: the first step whose
    /// distance is greater than the distance, or the last step's mesh
    /// index when none is. An entity with no LOD steps uses its own
    /// meshIndex.
    std::uint32_t lodMesh(std::size_t entityIndex, float distance) const;
    /// Statistics: the entity count, the partitioning's node count and
    /// the number of queries performed since the last reset.
    SceneStats stats() const;
    /// Resets the query counter.
    void resetStats();
private:
    struct Impl;
    Impl* m_impl;
};
}  // namespace scene
}  // namespace grace
