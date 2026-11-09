#include "grace/visual/null_visual_backend.hpp"

#include <cstdio>
#include <stdexcept>
#include <string>

namespace {

int g_checks = 0;
int g_failures = 0;

void check(bool condition, const std::string& message)
{
    ++g_checks;
    if (!condition)
    {
        ++g_failures;
        std::fprintf(stderr, "FAIL: %s\n", message.c_str());
    }
}

void checkEq(const std::string& actual, const std::string& expected,
             const std::string& message)
{
    ++g_checks;
    if (actual != expected)
    {
        ++g_failures;
        std::fprintf(stderr, "FAIL: %s (actual '%s', expected '%s')\n",
                     message.c_str(), actual.c_str(), expected.c_str());
    }
}

void testRegistryLifetime()
{
    grace::visual::NullVisualBackend backend;
    checkEq(backend.describe(), "null (headless)",
            "describe() returns the null backend description");
    check(backend.violationCount() == 0u, "no violations at start");

    grace::visual::VisualRegistry* registry = backend.createRegistry("blocks");
    check(registry != nullptr, "createRegistry returns a registry");

    bool threw = false;
    try
    {
        backend.createRegistry("");
    }
    catch (const std::invalid_argument&)
    {
        threw = true;
    }
    check(threw, "createRegistry with an empty name throws");

    threw = false;
    try
    {
        backend.createRegistry("blocks");
    }
    catch (const std::invalid_argument&)
    {
        threw = true;
    }
    check(threw, "createRegistry with a duplicate name throws");

    backend.releaseRegistry(registry);
    backend.releaseRegistry(registry);
    check(backend.violationCount() == 1u,
          "double releaseRegistry is a violation but stays safe");

    backend.releaseRegistry(nullptr);
    check(backend.violationCount() == 2u, "null releaseRegistry is a violation");

    backend.shutdown();
    backend.shutdown();
    check(backend.violationCount() == 3u,
          "a repeated shutdown() is a violation but stays safe");

    registry = backend.createRegistry("after");
    check(registry == nullptr,
          "createRegistry after shutdown is a violation and returns null");
    check(backend.violationCount() == 4u,
          "mutating calls after shutdown are violations");
}

void testRegistration()
{
    grace::visual::NullVisualBackend backend;
    grace::visual::VisualRegistry* registry = backend.createRegistry("blocks");

    grace::visual::BlockDesc start;
    start.name = "Start";
    start.kind = grace::visual::BlockKind::Flow;
    start.pins.push_back({"out", grace::visual::PinDirection::Output,
                          grace::visual::PinType::Flow});
    start.pins.push_back({"enter", grace::visual::PinDirection::Input,
                          grace::visual::PinType::Flow});

    backend.registerBlock(registry, start);
    check(backend.events().size() == 2u,
          "createRegistry and registerBlock are recorded in order");

    grace::visual::PinDesc pin;
    pin.name = "target";
    pin.direction = grace::visual::PinDirection::Input;
    pin.type = grace::visual::PinType::Int32;
    backend.registerPin(registry, "Start", pin);
    check(backend.events().size() == 3u,
          "registerPin is recorded after registerBlock");

    bool threw = false;
    try
    {
        backend.registerPin(registry, "Start", pin);
    }
    catch (const std::invalid_argument&)
    {
        threw = true;
    }
    check(threw, "registerPin with a duplicate pin name throws");

    threw = false;
    try
    {
        backend.registerPin(registry, "Missing", pin);
    }
    catch (const std::invalid_argument&)
    {
        threw = true;
    }
    check(threw, "registerPin on an unregistered block throws");

    grace::visual::BlockDesc bad;
    bad.name = "Bad";
    bad.kind = grace::visual::BlockKind::Flow;
    bad.pins.push_back({"value", grace::visual::PinDirection::Input,
                        grace::visual::PinType::Int32});
    threw = false;
    try
    {
        backend.registerBlock(registry, bad);
    }
    catch (const std::invalid_argument&)
    {
        threw = true;
    }
    check(threw, "registerBlock with an execution block without flow pins throws");

    grace::visual::BlockDesc named;
    named.name = "Named";
    threw = false;
    try
    {
        backend.registerBlock(registry, named);
    }
    catch (const std::invalid_argument&)
    {
        threw = true;
    }
    check(threw, "registerBlock with an empty pin list throws");

    check(backend.violationCount() == 0u,
          "no violations during registration");
    backend.shutdown();
    check(backend.violationCount() == 0u,
          "a single shutdown is not a violation");
}

void testGraphValidation()
{
    grace::visual::NullVisualBackend backend;
    grace::visual::VisualRegistry* registry = backend.createRegistry("blocks");

    grace::visual::BlockDesc start;
    start.name = "Start";
    start.kind = grace::visual::BlockKind::Flow;
    start.pins.push_back({"out", grace::visual::PinDirection::Output,
                          grace::visual::PinType::Flow});
    start.pins.push_back({"enter", grace::visual::PinDirection::Input,
                          grace::visual::PinType::Flow});
    backend.registerBlock(registry, start);

    grace::visual::BlockDesc print;
    print.name = "PrintInt";
    print.kind = grace::visual::BlockKind::Flow;
    print.pins.push_back({"enter", grace::visual::PinDirection::Input,
                          grace::visual::PinType::Flow});
    print.pins.push_back({"value", grace::visual::PinDirection::Input,
                          grace::visual::PinType::Int32});
    backend.registerBlock(registry, print);

    grace::visual::GraphDesc desc;
    desc.name = "main";
    desc.nodes.push_back({"Start", "begin"});
    desc.nodes.push_back({"PrintInt", "output"});
    desc.connections.push_back({"begin", "out", "output", "enter"});
    check(desc.validate(desc.name == "" ? desc.name : desc.name),
          "GraphDesc::validate passes on a sound graph");

    grace::visual::VisualGraph* graph = backend.createGraph(registry, desc);
    check(graph != nullptr, "createGraph returns a graph for a sound graph");

    grace::visual::GraphDesc bad;
    bad.name = "main";
    bad.nodes.push_back({"Missing", "begin"});
    bool threw = false;
    try
    {
        backend.createGraph(registry, bad);
    }
    catch (const std::invalid_argument&)
    {
        threw = true;
    }
    check(threw, "createGraph with an unregistered block throws");

    grace::visual::GraphDesc badNode;
    badNode.name = "main";
    badNode.nodes.push_back({"Start", "begin"});
    badNode.nodes.push_back({"PrintInt", "begin"});
    threw = false;
    try
    {
        backend.createGraph(registry, badNode);
    }
    catch (const std::invalid_argument&)
    {
        threw = true;
    }
    check(threw, "createGraph with duplicate node names throws");

    grace::visual::GraphDesc badConnection;
    badConnection.name = "main";
    badConnection.nodes.push_back({"Start", "begin"});
    badConnection.connections.push_back({"begin", "out", "ghost", "enter"});
    threw = false;
    try
    {
        backend.createGraph(registry, badConnection);
    }
    catch (const std::invalid_argument&)
    {
        threw = true;
    }
    check(threw, "createGraph with a connection to a missing node throws");

    grace::visual::GraphDesc emptyName;
    emptyName.nodes.push_back({"Start", "begin"});
    threw = false;
    try
    {
        backend.createGraph(registry, emptyName);
    }
    catch (const std::invalid_argument&)
    {
        threw = true;
    }
    check(threw, "createGraph with an empty name throws");

    backend.releaseGraph(graph);
    backend.releaseGraph(graph);
    check(backend.violationCount() == 1u,
          "double releaseGraph is a violation but stays safe");

    backend.shutdown();
    check(backend.violationCount() == 1u,
          "the single shutdown is not a violation");
}

void testCompile()
{
    grace::visual::NullVisualBackend backend;
    grace::visual::VisualRegistry* registry = backend.createRegistry("blocks");

    grace::visual::BlockDesc start;
    start.name = "Start";
    start.kind = grace::visual::BlockKind::Flow;
    start.pins.push_back({"out", grace::visual::PinDirection::Output,
                          grace::visual::PinType::Flow});
    start.pins.push_back({"enter", grace::visual::PinDirection::Input,
                          grace::visual::PinType::Flow});
    backend.registerBlock(registry, start);

    grace::visual::GraphDesc desc;
    desc.name = "main";
    desc.nodes.push_back({"Start", "begin"});
    grace::visual::VisualGraph* graph = backend.createGraph(registry, desc);

    check(!backend.compile(graph, "int main()"),
          "compile() fails: the null backend has no language");
    checkEq(backend.lastError(),
            "null visual backend: no language, cannot compile graphs",
            "lastError() carries the deterministic message");

    check(!backend.compile(nullptr, "int main()"),
          "compile() on a null graph fails");
    check(backend.violationCount() == 1u,
          "compile() on a null graph is a violation");

    grace::visual::NullVisualBackend foreign;
    check(!foreign.compile(graph, "int main()"),
          "compile() on a foreign graph fails");
    check(foreign.violationCount() == 1u,
          "compile() on a foreign graph is a violation");

    backend.shutdown();
    check(!backend.compile(graph, "int main()"),
          "compile() after shutdown fails");
    check(backend.violationCount() == 2u,
          "the null graph compile and the after-shutdown compile are "
          "violations");
}

void testGraphOwnership()
{
    grace::visual::NullVisualBackend backend;
    grace::visual::VisualRegistry* registry = backend.createRegistry("blocks");

    grace::visual::BlockDesc start;
    start.name = "Start";
    start.kind = grace::visual::BlockKind::Flow;
    start.pins.push_back({"out", grace::visual::PinDirection::Output,
                          grace::visual::PinType::Flow});
    start.pins.push_back({"enter", grace::visual::PinDirection::Input,
                          grace::visual::PinType::Flow});
    backend.registerBlock(registry, start);

    grace::visual::GraphDesc desc;
    desc.name = "main";
    desc.nodes.push_back({"Start", "begin"});
    grace::visual::VisualGraph* graph = backend.createGraph(registry, desc);
    check(graph != nullptr, "createGraph returns a graph");

    backend.releaseRegistry(registry);
    check(backend.violationCount() == 0u,
          "releaseRegistry releases the graphs it owns");

    backend.releaseGraph(graph);
    check(backend.violationCount() == 1u,
          "releasing a graph owned by a released registry is a violation");

    backend.shutdown();
    check(backend.violationCount() == 1u,
          "the shutdown is not a violation");
}

}  // namespace

int main()
{
    testRegistryLifetime();
    testRegistration();
    testGraphValidation();
    testCompile();
    testGraphOwnership();

    std::printf("visual: %d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
