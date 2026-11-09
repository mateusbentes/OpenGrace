// The scene suite: the data-driven entities, the selectable spatial
// partitioning (flat, grid, octree, quadtree), the queries, the level of
// detail and the serialisation strategy (decision 36).
//
// The suite covers the documented architecture (section 3.8) as
// statements:
// - the partitioning is selectable among flat, grid, octree and quadtree,
//   and the same scene produces the same query results in the same order
//   under every choice;
// - the queries walk the partitioning, so a query outside the world
//   returns nothing and a query inside returns the entities it touches;
// - the LOD picks the mesh index by distance, and an entity with no LOD
//   steps uses its own mesh index;
// - the serialisation strategy (decision 36): the same entities produce
//   byte-identical bytes, the reader accepts exactly what the writer
//   produces, and the scene's state round trips through the OWD
//   container as one entry;
// - the refusals: duplicate names, empty names, zero scale, non-ascending
//   LOD distances, a zero grid resolution, an inverted world extent, a
//   truncated payload.
#include "grace/owd/owd.hpp"
#include "grace/scene/scene.hpp"
#include "grace/scene/scene_serial.hpp"
#include <cstdio>
#include <functional>
#include <stdexcept>
#include <string>
#include <vector>
#include "test_support.hpp"
using grace::owd::Loader;
using grace::owd::PackDesc;
using grace::owd::Reader;
using grace::owd::Writer;
using grace::owd::Payload;
using grace::scene::EntityDesc;
using grace::scene::LodStep;
using grace::scene::Partitioning;
using grace::scene::Scene;
using grace::scene::SceneError;
using grace::scene::SerialError;
namespace {
// A deterministic scene: a grid of entities, each with a name that carries
// its index, and a LOD step at a fixed distance.
std::vector<EntityDesc> makeGrid(std::uint32_t count)
{
    std::vector<EntityDesc> entities;
    for (std::uint32_t i = 0; i < count; ++i)
    {
        EntityDesc entity;
        entity.name = "entity/" + std::to_string(i);
        entity.position = {static_cast<float>(i) - 0.5f, 0.0f, 0.0f};
        entity.meshIndex = i;
        entity.materialIndex = 0;
        entity.lods.push_back(LodStep{10.0f, i + 1000});
        entities.push_back(std::move(entity));
    }
    return entities;
}
bool throwsSceneError(const std::function<void()>& fn)
{
    try
    {
        fn();
    }
    catch (const SceneError&)
    {
        return true;
    }
    return false;
}
void testEntityValidation()
{
    EntityDesc entity;
    std::string message;
    // The empty name is invalid.
    GRACE_CHECK(!entity.validate(message));
    entity.name = "entity/0";
    GRACE_CHECK(entity.validate(message));
    // The zero scale is invalid.
    entity.scale = {0.0f, 1.0f, 1.0f};
    GRACE_CHECK(!entity.validate(message));
    entity.scale = {1.0f, 1.0f, 1.0f};
    // The non-ascending LOD distances are invalid.
    entity.lods.push_back(LodStep{10.0f, 1});
    entity.lods.push_back(LodStep{5.0f, 2});
    GRACE_CHECK(!entity.validate(message));
    // Strictly ascending passes.
    entity.lods[1].distance = 10.0f;
    GRACE_CHECK(!entity.validate(message));
    entity.lods[1].distance = 20.0f;
    GRACE_CHECK(entity.validate(message));
}
void testAddAndRefusals()
{
    Scene scene;
    EntityDesc entity;
    entity.name = "entity/0";
    scene.add(entity);
    GRACE_CHECK(scene.count() == 1);
    // The duplicate name is a SceneError.
    GRACE_CHECK(throwsSceneError([&] { scene.add(entity); }));
    // The empty name is a SceneError.
    EntityDesc empty;
    GRACE_CHECK(throwsSceneError([&] { scene.add(empty); }));
    // The zero scale is a SceneError.
    EntityDesc zero;
    zero.name = "entity/1";
    zero.scale = {0.0f, 1.0f, 1.0f};
    GRACE_CHECK(throwsSceneError([&] { scene.add(zero); }));
    // The non-ascending LOD distances are a SceneError.
    EntityDesc lods;
    lods.name = "entity/2";
    lods.lods.push_back(LodStep{10.0f, 1});
    lods.lods.push_back(LodStep{5.0f, 2});
    GRACE_CHECK(throwsSceneError([&] { scene.add(lods); }));
    // The scene is unchanged: the three refusals added nothing.
    GRACE_CHECK(scene.count() == 1);
}
void testFlatQuery()
{
    Scene scene;
    for (const EntityDesc& entity : makeGrid(4))
        scene.add(entity);
    // The flat partitioning is the default: no partitioning, every query
    // walks every entity. The order is deterministic: ascending index.
    GRACE_CHECK(scene.partitioning() == Partitioning::Flat);
    grace::scene::QueryResult result =
        scene.query({-0.6f, -1.0f, -1.0f}, {0.0f, 1.0f, 1.0f});
    GRACE_CHECK(result.entityIndices.size() == 1);
    GRACE_CHECK(result.entityIndices[0] == 0);
    // A query outside the world returns nothing.
    GRACE_CHECK(scene.query({100.0f, 100.0f, 100.0f},
                            {101.0f, 101.0f, 101.0f})
                    .entityIndices.empty());
    // The query count is recorded.
    GRACE_CHECK(scene.stats().queryCount == 2);
    scene.resetStats();
    GRACE_CHECK(scene.stats().queryCount == 0);
}
void testPartitioningSelection()
{
    // The same entities and the same query produce the same result under
    // every partitioning, in the same order: the partitioning is a query
    // strategy, not a data reorganisation.
    const std::vector<EntityDesc> entities = makeGrid(16);
    for (const Partitioning kind :
         {Partitioning::Flat, Partitioning::Grid, Partitioning::Octree,
          Partitioning::Quadtree})
    {
        Scene scene;
        for (const EntityDesc& entity : entities)
            scene.add(entity);
        scene.setPartitioning(kind, {-1.0f, -1.0f, -1.0f},
                              {16.0f, 1.0f, 1.0f}, 4);
        GRACE_CHECK(scene.partitioning() == kind);
        const grace::scene::QueryResult result =
            scene.query({-0.6f, -1.0f, -1.0f}, {0.5f, 1.0f, 1.0f});
        GRACE_CHECK(result.entityIndices.size() == 2);
        GRACE_CHECK(result.entityIndices[0] == 0);
        GRACE_CHECK(result.entityIndices[1] == 1);
    }
    // The partitioning is re-selectable: the same scene can be
    // re-partitioned without rebuilding the entities.
    Scene scene;
    for (const EntityDesc& entity : entities)
        scene.add(entity);
    scene.setPartitioning(Partitioning::Grid, {-1.0f, -1.0f, -1.0f},
                          {16.0f, 1.0f, 1.0f}, 4);
    GRACE_CHECK(scene.partitioning() == Partitioning::Grid);
    scene.setPartitioning(Partitioning::Octree, {-1.0f, -1.0f, -1.0f},
                          {16.0f, 1.0f, 1.0f}, 4);
    GRACE_CHECK(scene.partitioning() == Partitioning::Octree);
    GRACE_CHECK(scene.count() == 16);
}
void testTreeQueries()
{
    // The tree partitionings are queried through the nodes they touch. A
    // query touching only one subtree costs that subtree's nodes.
    Scene scene;
    for (const EntityDesc& entity : makeGrid(16))
        scene.add(entity);
    scene.setPartitioning(Partitioning::Octree, {-1.0f, -1.0f, -1.0f},
                          {16.0f, 1.0f, 1.0f}, 4);
    GRACE_CHECK(scene.stats().nodeCount > 0);
    const grace::scene::QueryResult result =
        scene.query({-0.6f, -1.0f, -1.0f}, {0.5f, 1.0f, 1.0f});
    GRACE_CHECK(result.entityIndices.size() == 2);
    // The same query twice gives the same result, in the same order.
    const grace::scene::QueryResult again =
        scene.query({-0.6f, -1.0f, -1.0f}, {0.5f, 1.0f, 1.0f});
    GRACE_CHECK(again.entityIndices == result.entityIndices);
}
void testPartitioningRefusals()
{
    Scene scene;
    EntityDesc entity;
    entity.name = "entity/0";
    entity.position = {0.0f, 0.0f, 0.0f};
    scene.add(entity);
    // A zero grid resolution is a SceneError.
    GRACE_CHECK(throwsSceneError([&] {
        scene.setPartitioning(Partitioning::Grid, {-1.0f, -1.0f, -1.0f},
                              {1.0f, 1.0f, 1.0f}, 0);
    }));
    // An inverted world extent is a SceneError.
    GRACE_CHECK(throwsSceneError([&] {
        scene.setPartitioning(Partitioning::Grid, {1.0f, 1.0f, 1.0f},
                              {-1.0f, -1.0f, -1.0f}, 4);
    }));
    // An inverted query extent is a SceneError.
    GRACE_CHECK(throwsSceneError(
        [&] { scene.query({1.0f, 1.0f, 1.0f}, {-1.0f, -1.0f, -1.0f}); }));
    // The scene is unchanged: the refusals did not change the
    // partitioning or lose the entities.
    GRACE_CHECK(scene.count() == 1);
    GRACE_CHECK(scene.partitioning() == Partitioning::Flat);
}
void testLod()
{
    Scene scene;
    EntityDesc entity;
    entity.name = "entity/0";
    entity.meshIndex = 7;
    entity.lods.push_back(LodStep{10.0f, 100});
    entity.lods.push_back(LodStep{20.0f, 200});
    scene.add(entity);
    // The first step whose distance is greater than the distance.
    GRACE_CHECK(scene.lodMesh(0, 0.0f) == 7);
    GRACE_CHECK(scene.lodMesh(0, 9.0f) == 7);
    GRACE_CHECK(scene.lodMesh(0, 10.0f) == 100);
    GRACE_CHECK(scene.lodMesh(0, 19.0f) == 100);
    GRACE_CHECK(scene.lodMesh(0, 20.0f) == 200);
    GRACE_CHECK(scene.lodMesh(0, 100.0f) == 200);
    // An entity with no LOD steps uses its own mesh index.
    EntityDesc plain;
    plain.name = "entity/1";
    plain.meshIndex = 42;
    scene.add(plain);
    GRACE_CHECK(scene.lodMesh(1, 100.0f) == 42);
}
void testSerialisationDeterministic()
{
    const std::vector<EntityDesc> entities = makeGrid(8);
    const std::vector<std::uint8_t> first = grace::scene::serialise(entities);
    const std::vector<std::uint8_t> second =
        grace::scene::serialise(entities);
    GRACE_CHECK(first.size() > 0);
    GRACE_CHECK(first == second);
}
void testSerialisationRoundTrip()
{
    const std::vector<EntityDesc> entities = makeGrid(8);
    const std::vector<EntityDesc> back =
        grace::scene::deserialise(grace::scene::serialise(entities));
    GRACE_CHECK(back.size() == entities.size());
    for (std::size_t i = 0; i < entities.size(); ++i)
    {
        GRACE_CHECK(back[i].name == entities[i].name);
        GRACE_CHECK(back[i].position == entities[i].position);
        GRACE_CHECK(back[i].scale == entities[i].scale);
        GRACE_CHECK(back[i].meshIndex == entities[i].meshIndex);
        GRACE_CHECK(back[i].materialIndex == entities[i].materialIndex);
        GRACE_CHECK(back[i].lods.size() == entities[i].lods.size());
        for (std::size_t lod = 0; lod < entities[i].lods.size(); ++lod)
        {
            GRACE_CHECK(back[i].lods[lod].distance ==
                        entities[i].lods[lod].distance);
            GRACE_CHECK(back[i].lods[lod].meshIndex ==
                        entities[i].lods[lod].meshIndex);
        }
    }
}
void testSerialisationRefusals()
{
    // A truncated payload is a SerialError: the reader refuses to hand
    // out entities it did not fully read.
    const std::vector<EntityDesc> entities = makeGrid(4);
    const std::vector<std::uint8_t> bytes = grace::scene::serialise(entities);
    bool threw = false;
    try
    {
        grace::scene::deserialise(std::vector<std::uint8_t>(
            bytes.begin(), bytes.begin() + static_cast<std::ptrdiff_t>(
                                                 bytes.size() / 2)));
    }
    catch (const SerialError&)
    {
        threw = true;
    }
    GRACE_CHECK(threw);
    // A payload that is not a scene is a SerialError.
    threw = false;
    try
    {
        grace::scene::deserialise(
            {'N', 'O', 'P', 'E', 0, 0, 0, 0, 0, 0, 0, 0});
    }
    catch (const SerialError&)
    {
        threw = true;
    }
    GRACE_CHECK(threw);
    // Trailing bytes are a SerialError.
    threw = false;
    try
    {
        std::vector<std::uint8_t> trailing = grace::scene::serialise(entities);
        trailing.push_back(0);
        grace::scene::deserialise(trailing);
    }
    catch (const SerialError&)
    {
        threw = true;
    }
    GRACE_CHECK(threw);
}
void testSerialisationThroughOwd()
{
    // The serialisation strategy (decision 36): the scene's state writes
    // into the OWD container as one entry, and the reader reads back.
    // The container's own checksums and the overlay loader carry the
    // integrity.
    Scene scene;
    for (const EntityDesc& entity : makeGrid(4))
        scene.add(entity);
    Writer writer;
    PackDesc desc;
    desc.name = "scenes/level";
    desc.kind = 1;
    desc.payload = grace::scene::serialise(scene.entities());
    writer.add(desc);
    const std::vector<std::uint8_t> container = writer.finish();
    Reader reader(container.data(), container.size());
    GRACE_CHECK(reader.has("scenes/level"));
    const grace::owd::Payload payload = reader.read("scenes/level");
    const std::vector<EntityDesc> back =
        grace::scene::deserialise(payload.bytes);
    GRACE_CHECK(back.size() == 4);
    GRACE_CHECK(back[0].name == "entity/0");
    GRACE_CHECK(back[3].name == "entity/3");
    // The overlay loader composes: a later provider overrides an earlier
    // one, so the same entry name can hold a different scene.
    Writer overlay;
    PackDesc overlayDesc;
    overlayDesc.name = "scenes/level";
    overlayDesc.kind = 1;
    std::vector<EntityDesc> override = makeGrid(1);
    overlayDesc.payload = grace::scene::serialise(override);
    overlay.add(overlayDesc);
    const std::vector<std::uint8_t> overlayBytes = overlay.finish();
    Reader overlayReader(overlayBytes.data(), overlayBytes.size());
    Loader loader;
    loader.add(&reader);
    loader.add(&overlayReader);
    GRACE_CHECK(loader.count() == 2);
    Reader* resolved = loader.find("scenes/level");
    GRACE_CHECK(resolved == &overlayReader);
    const grace::owd::Payload overlayPayload = resolved->read("scenes/level");
    const std::vector<EntityDesc> overlayBack =
        grace::scene::deserialise(overlayPayload.bytes);
    GRACE_CHECK(overlayBack.size() == 1);
}
}  // namespace
int main()
{
    testEntityValidation();
    testAddAndRefusals();
    testFlatQuery();
    testPartitioningSelection();
    testTreeQueries();
    testPartitioningRefusals();
    testLod();
    testSerialisationDeterministic();
    testSerialisationRoundTrip();
    testSerialisationRefusals();
    testSerialisationThroughOwd();
    return grace::test::summary("scene");
}
