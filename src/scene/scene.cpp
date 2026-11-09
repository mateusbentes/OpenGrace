// The scene module's implementation: data-driven entities, selectable
// spatial partitioning (flat, grid, octree, quadtree), scene queries and
// level of detail (docs/grace-architecture.md section 3.8).
//
// Everything is deterministic: the same entities and the same
// partitioning always produce the same query results in the same order.
// The queries walk the partitioning, so their cost is the nodes they
// touch, not the whole scene.
#include "grace/scene/scene.hpp"
#include <algorithm>
#include <cmath>
#include <stdexcept>
namespace grace {
namespace scene {
namespace {
// The tree partitionings subdivide a node once it holds more than this
// many entities, and stop subdividing at this depth. Both are fixed
// constants, so the same scene always builds the same tree.
inline constexpr std::size_t kLeafCapacity = 8;
inline constexpr std::uint32_t kMaxDepth = 8;
// A node's AABB: the world-space minimum and maximum corners.
struct NodeBounds
{
    core::vec3 min;
    core::vec3 max;
};
bool contains(const NodeBounds& b, const core::vec3& p)
{
    return p.x >= b.min.x && p.x <= b.max.x && p.y >= b.min.y &&
           p.y <= b.max.y && p.z >= b.min.z && p.z <= b.max.z;
}
bool intersects(const NodeBounds& a, const NodeBounds& b)
{
    return a.min.x <= b.max.x && a.max.x >= b.min.x &&
           a.min.y <= b.max.y && a.max.y >= b.min.y &&
           a.min.z <= b.max.z && a.max.z >= b.min.z;
}
// A tree node: the bounds, the entity indices that live here, and the
// children (empty for a leaf). A subdivided node owns no entities: the
// walk would otherwise visit the same entity once per node it lands in,
// so the entities move into the children and the parent stays empty.
struct TreeNode
{
    NodeBounds bounds;
    std::vector<std::uint32_t> entities;
    std::vector<std::uint32_t> children;
};
// Which child an entity lands in, for the octree (3 bits) and the
// quadtree (2 bits). The child that is "high" on an axis uses a strict
// greater-than against the split plane, so a point exactly on the plane
// lands in the low child only and never in two children.
std::uint32_t childIndex(const NodeBounds& bounds, const core::vec3& p,
                         bool quad)
{
    const float midX = (bounds.min.x + bounds.max.x) * 0.5f;
    const float midZ = (bounds.min.z + bounds.max.z) * 0.5f;
    std::uint32_t index = 0;
    if (p.x > midX)
        index |= 1u;
    if (p.z > midZ)
        index |= 2u;
    if (!quad)
    {
        const float midY = (bounds.min.y + bounds.max.y) * 0.5f;
        if (p.y > midY)
            index |= 4u;
    }
    return index;
}
}  // namespace
class Scene::Impl
{
public:
    std::vector<EntityDesc> entities;
    Partitioning kind = Partitioning::Flat;
    NodeBounds world{};
    std::uint32_t gridRes = 1;
    std::vector<TreeNode> nodes;
    std::uint32_t queryCount = 0;
    // The grid: cell (x, z) indices. The y axis is not partitioned; the
    // query filters y per entity. Each axis computes its own index over
    // its own extent.
    std::uint32_t cellAxis(float value, float minV, float maxV,
                           std::uint32_t res) const
    {
        const float span = maxV - minV;
        if (span <= 0.0f)
            return 0;
        const float t = (value - minV) / span;
        const float clamped = std::min(std::max(t, 0.0f), 1.0f);
        // A value exactly at the far end lands in the last cell.
        std::uint32_t index =
            static_cast<std::uint32_t>(clamped * static_cast<float>(res));
        if (index >= res)
            index = res - 1;
        return index;
    }
    std::uint32_t cellX(float x) const
    {
        return cellAxis(x, world.min.x, world.max.x, gridRes);
    }
    std::uint32_t cellZ(float z) const
    {
        return cellAxis(z, world.min.z, world.max.z, gridRes);
    }
    void buildGrid()
    {
        nodes.clear();
        nodes.resize(static_cast<std::size_t>(gridRes) * gridRes);
        const float res = static_cast<float>(gridRes);
        const float cellSizeX = (world.max.x - world.min.x) / res;
        const float cellSizeZ = (world.max.z - world.min.z) / res;
        for (std::uint32_t cz = 0; cz < gridRes; ++cz)
        {
            for (std::uint32_t cx = 0; cx < gridRes; ++cx)
            {
                const float fx = static_cast<float>(cx);
                const float fz = static_cast<float>(cz);
                TreeNode& node = nodes[cz * gridRes + cx];
                node.bounds.min = {world.min.x + fx * cellSizeX,
                                   world.min.y,
                                   world.min.z + fz * cellSizeZ};
                node.bounds.max = {node.bounds.min.x + cellSizeX,
                                   world.max.y,
                                   node.bounds.min.z + cellSizeZ};
            }
        }
        for (std::size_t i = 0; i < entities.size(); ++i)
        {
            const core::vec3& p = entities[i].position;
            const std::uint32_t ix = cellX(p.x);
            const std::uint32_t iz = cellZ(p.z);
            nodes[iz * gridRes + ix].entities.push_back(
                static_cast<std::uint32_t>(i));
        }
    }
    // The tree partitionings: build a subtree covering the bounds, and
    // subdivide until the leaf capacity or the depth limit. The split
    // plane is the bounds' midpoint on every axis the partitioning
    // splits. The quadtree ignores the y axis; the octree splits all
    // three. The child that is "high" on an axis uses a strict
    // greater-than against the split plane, so a point exactly on the
    // plane lands in the low child only and never in two children.
    std::uint32_t buildTree(const NodeBounds& bounds,
                            const std::vector<std::uint32_t>& indices,
                            std::uint32_t depth, bool quad)
    {
        const std::uint32_t id = static_cast<std::uint32_t>(nodes.size());
        if (indices.size() <= kLeafCapacity || depth >= kMaxDepth)
        {
            nodes.push_back(TreeNode{bounds, indices, {}});
            return id;
        }
        nodes.push_back(TreeNode{bounds, {}, {}});
        const float midX = (bounds.min.x + bounds.max.x) * 0.5f;
        const float midY =
            quad ? bounds.min.y : (bounds.min.y + bounds.max.y) * 0.5f;
        const float midZ = (bounds.min.z + bounds.max.z) * 0.5f;
        const std::uint32_t childCount = quad ? 4u : 8u;
        std::vector<std::vector<std::uint32_t>> buckets(childCount);
        for (const std::uint32_t e : indices)
        {
            const std::uint32_t child =
                childIndex(bounds, entities[e].position, quad);
            buckets[child].push_back(e);
        }
        std::vector<std::uint32_t> childIds;
        for (std::uint32_t i = 0; i < childCount; ++i)
        {
            // The child bounds: the low child on an axis is the closed
            // lower half; the high child is the open upper half. An
            // entity exactly on the split plane lands in the low child.
            const bool highX = (i & 1u) != 0;
            const bool highZ = (i & 2u) != 0;
            const bool highY = !quad && (i & 4u) != 0;
            NodeBounds child;
            child.min = {highX ? midX : bounds.min.x,
                         highY ? midY : bounds.min.y,
                         highZ ? midZ : bounds.min.z};
            child.max = {highX ? bounds.max.x : midX,
                         highY ? bounds.max.y : midY,
                         highZ ? bounds.max.z : midZ};
            childIds.push_back(
                buildTree(child, buckets[i], depth + 1, quad));
        }
        nodes[id].children = childIds;
        return id;
    }
    void buildTreePartitioning(bool quad)
    {
        nodes.clear();
        std::vector<std::uint32_t> all;
        all.reserve(entities.size());
        for (std::size_t i = 0; i < entities.size(); ++i)
            all.push_back(static_cast<std::uint32_t>(i));
        buildTree(world, all, 0, quad);
    }
    void walk(const std::uint32_t nodeId, const NodeBounds& query,
              std::vector<std::uint32_t>& out) const
    {
        const TreeNode& node = nodes[nodeId];
        if (!intersects(node.bounds, query))
            return;
        for (const std::uint32_t e : node.entities)
        {
            if (contains(query, entities[e].position))
                out.push_back(e);
        }
        for (const std::uint32_t child : node.children)
            walk(child, query, out);
    }
    void collect(const NodeBounds& query,
                 std::vector<std::uint32_t>& out) const
    {
        switch (kind)
        {
        case Partitioning::Flat:
            // No partitioning: every query walks every entity.
            for (std::size_t i = 0; i < entities.size(); ++i)
            {
                if (contains(query, entities[i].position))
                    out.push_back(static_cast<std::uint32_t>(i));
            }
            break;
        case Partitioning::Grid:
        {
            if (nodes.empty())
                break;
            const std::uint32_t x0 = cellX(query.min.x);
            const std::uint32_t x1 = cellX(query.max.x);
            const std::uint32_t z0 = cellZ(query.min.z);
            const std::uint32_t z1 = cellZ(query.max.z);
            for (std::uint32_t cz = z0; cz <= z1; ++cz)
            {
                for (std::uint32_t cx = x0; cx <= x1; ++cx)
                {
                    const TreeNode& node = nodes[cz * gridRes + cx];
                    if (!intersects(node.bounds, query))
                        continue;
                    for (const std::uint32_t e : node.entities)
                    {
                        if (contains(query, entities[e].position))
                            out.push_back(e);
                    }
                }
            }
            break;
        }
        case Partitioning::Octree:
        case Partitioning::Quadtree:
            // The tree partitionings walk the nodes the query touches.
            if (!nodes.empty())
                walk(0, query, out);
            break;
        }
    }
};
SceneError::SceneError(const std::string& message)
    : std::runtime_error(message)
{
}
Scene::Scene() : m_impl(new Impl) {}
Scene::~Scene() { delete m_impl; }
void Scene::add(const EntityDesc& desc)
{
    std::string message;
    if (!desc.validate(message))
        throw SceneError(message);
    for (const EntityDesc& existing : m_impl->entities)
    {
        if (existing.name == desc.name)
        {
            throw SceneError(
                "scene: a duplicate entity name is invalid");
        }
    }
    m_impl->entities.push_back(desc);
    // The partitioning is built on the added entities; the scene
    // re-partitions on the next setPartitioning() call, not on the add.
    if (m_impl->kind == Partitioning::Grid)
        m_impl->buildGrid();
    else if (m_impl->kind == Partitioning::Octree)
        m_impl->buildTreePartitioning(false);
    else if (m_impl->kind == Partitioning::Quadtree)
        m_impl->buildTreePartitioning(true);
}
std::size_t Scene::count() const { return m_impl->entities.size(); }
const EntityDesc& Scene::entity(std::size_t index) const
{
    return m_impl->entities[index];
}
const std::vector<EntityDesc>& Scene::entities() const
{
    return m_impl->entities;
}
void Scene::setPartitioning(Partitioning kind, const core::vec3& worldMin,
                            const core::vec3& worldMax,
                            std::uint32_t gridRes)
{
    if (kind != Partitioning::Flat && gridRes == 0)
        throw SceneError("scene: a zero grid resolution is invalid");
    if (kind != Partitioning::Flat &&
        (worldMin.x > worldMax.x || worldMin.y > worldMax.y ||
         worldMin.z > worldMax.z))
    {
        throw SceneError("scene: an inverted world extent is invalid");
    }
    m_impl->kind = kind;
    m_impl->world = NodeBounds{worldMin, worldMax};
    m_impl->gridRes = kind == Partitioning::Flat ? 1 : gridRes;
    if (m_impl->kind == Partitioning::Grid)
        m_impl->buildGrid();
    else if (m_impl->kind == Partitioning::Octree)
        m_impl->buildTreePartitioning(false);
    else if (m_impl->kind == Partitioning::Quadtree)
        m_impl->buildTreePartitioning(true);
    else
        m_impl->nodes.clear();
}
Partitioning Scene::partitioning() const { return m_impl->kind; }
QueryResult Scene::query(const core::vec3& min, const core::vec3& max) const
{
    if (min.x > max.x || min.y > max.y || min.z > max.z)
        throw SceneError("scene: an inverted query extent is invalid");
    ++m_impl->queryCount;
    QueryResult result;
    std::vector<std::uint32_t> out;
    const NodeBounds query{min, max};
    m_impl->collect(query, out);
    // The order is deterministic: ascending entity index.
    std::sort(out.begin(), out.end());
    result.entityIndices = std::move(out);
    return result;
}
std::uint32_t Scene::lodMesh(std::size_t entityIndex, float distance) const
{
    const EntityDesc& entity = m_impl->entities[entityIndex];
    if (entity.lods.empty())
        return entity.meshIndex;
    // A step is the distance at which the mesh changes to that step's
    // mesh index. Below the first step's distance the entity uses its
    // own mesh index; the last step whose distance was reached wins.
    std::uint32_t mesh = entity.meshIndex;
    for (const LodStep& step : entity.lods)
    {
        if (distance >= step.distance)
            mesh = step.meshIndex;
    }
    return mesh;
}
SceneStats Scene::stats() const
{
    SceneStats stats;
    stats.entityCount = m_impl->entities.size();
    stats.nodeCount = m_impl->nodes.size();
    stats.queryCount = m_impl->queryCount;
    return stats;
}
void Scene::resetStats() { m_impl->queryCount = 0; }
}  // namespace scene
}  // namespace grace
