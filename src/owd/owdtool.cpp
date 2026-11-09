// The OWD tool: the command line that packs and unpacks OWD containers.
// The tool exercises the writer and the reader end to end: it is both the
// compression/decompression utility the engine ships with and the
// round-trip check the build runs.
//
// Usage:
//   owdtool pack <dir> <out.owd>   pack a directory tree into a container
//   owdtool unpack <file.owd> <dir>  unpack a container into a directory
//   owdtool list <file.owd>          print the entries

#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

#include "grace/owd/owd.hpp"

using grace::owd::EntryDesc;
using grace::owd::PackDesc;
using grace::owd::Reader;
using grace::owd::ReaderError;
using grace::owd::Writer;
using grace::owd::WriterError;

namespace {

// The filter is chosen per payload: the shuffle is applied to records of
// 4 bytes or more, and the delta to records of 4 bytes or more whose values
// a monotonic sweep would make sense for (indices, offsets). The tool
// cannot know the payload's shape, so it shuffles every record-shaped
// payload and deflates the result.
struct PackOptions
{
    std::filesystem::path root;
    std::filesystem::path out;
};

void printUsage()
{
    std::printf(
        "owdtool: the OWD container tool\n"
        "\n"
        "Usage:\n"
        "  owdtool pack <dir> <out.owd>\n"
        "  owdtool unpack <file.owd> <dir>\n"
        "  owdtool list <file.owd>\n");
}

int runPack(const std::filesystem::path& root,
            const std::filesystem::path& out)
{
    Writer writer;

    for (const auto& entry :
         std::filesystem::recursive_directory_iterator(root))
    {
        if (!entry.is_regular_file())
        {
            continue;
        }
        const std::string name = std::filesystem::relative(entry.path(), root)
                                     .generic_string();
        std::ifstream file(entry.path(), std::ios::binary);
        if (!file)
        {
            std::printf("owdtool: cannot read '%s'\n", name.c_str());
            return 1;
        }
        PackDesc desc;
        desc.name = name;
        desc.flags = 0;
        desc.payload.assign(std::istreambuf_iterator<char>(file),
                            std::istreambuf_iterator<char>());
        writer.add(desc);
    }

    const std::vector<std::uint8_t> bytes = writer.finish();
    std::ofstream file(out, std::ios::binary);
    if (!file)
    {
        std::printf("owdtool: cannot write '%s'\n", out.string().c_str());
        return 1;
    }
    file.write(reinterpret_cast<const char*>(bytes.data()),
               static_cast<std::streamsize>(bytes.size()));
    if (!file)
    {
        std::printf("owdtool: cannot write '%s'\n", out.string().c_str());
        return 1;
    }
    std::printf("owdtool: packed %zu entries, %zu bytes\n", writer.count(),
                bytes.size());
    return 0;
}

int runUnpack(const std::filesystem::path& file,
              const std::filesystem::path& outRoot)
{
    std::ifstream in(file, std::ios::binary);
    if (!in)
    {
        std::printf("owdtool: cannot read '%s'\n", file.string().c_str());
        return 1;
    }
    const std::vector<std::uint8_t> bytes(
        (std::istreambuf_iterator<char>(in)),
        std::istreambuf_iterator<char>());
    Reader reader(bytes.data(), bytes.size());

    for (const EntryDesc& desc : reader.entries())
    {
        const std::filesystem::path path = outRoot / desc.name;
        std::filesystem::create_directories(path.parent_path());
        const grace::owd::Payload payload = reader.read(desc.name);
        std::ofstream out(path, std::ios::binary);
        if (!out)
        {
            std::printf("owdtool: cannot write '%s'\n", desc.name.c_str());
            return 1;
        }
        out.write(reinterpret_cast<const char*>(payload.bytes.data()),
                  static_cast<std::streamsize>(payload.bytes.size()));
        if (!out)
        {
            std::printf("owdtool: cannot write '%s'\n", desc.name.c_str());
            return 1;
        }
    }
    std::printf("owdtool: unpacked %zu entries\n", reader.entries().size());
    return 0;
}

int runList(const std::filesystem::path& file)
{
    std::ifstream in(file, std::ios::binary);
    if (!in)
    {
        std::printf("owdtool: cannot read '%s'\n", file.string().c_str());
        return 1;
    }
    const std::vector<std::uint8_t> bytes(
        (std::istreambuf_iterator<char>(in)),
        std::istreambuf_iterator<char>());
    Reader reader(bytes.data(), bytes.size());
    for (const EntryDesc& desc : reader.entries())
    {
        std::printf("%s kind=%u inflated=%llu\n", desc.name.c_str(),
                    desc.kind,
                    static_cast<unsigned long long>(desc.payloadSizeInflated));
    }
    return 0;
}

}  // namespace

int main(int argc, char** argv)
{
    if (argc == 4 && std::strcmp(argv[1], "pack") == 0)
    {
        return runPack(argv[2], argv[3]);
    }
    if (argc == 4 && std::strcmp(argv[1], "unpack") == 0)
    {
        return runUnpack(argv[2], argv[3]);
    }
    if (argc == 3 && std::strcmp(argv[1], "list") == 0)
    {
        return runList(argv[2]);
    }
    printUsage();
    return 2;
}
