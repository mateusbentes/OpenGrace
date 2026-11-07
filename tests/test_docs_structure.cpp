// Structural checks on docs/implementation-status.md.
//
// Why this test exists. Every rule below was written after a defect that
// reached a commit in this repository, and not one of them was caught by the
// compiler, by any other suite, or by tools/verify-published-tree.sh. They were
// all found by a person reading a document:
//
//   R1  The decision numbering stopped being ascending. A script counted the
//       eight bold list items in the container specification as decisions, so
//       the numbers it computed were wrong.
//   R2  A decision number was reused. Two decisions were both 24, one about
//       the resource container and one about volumetric lights.
//   R3  A level-2 heading ended up inside the decision span, because a reorder
//       treated the last block as extending to the end of the file.
//   R4  A section was left empty. The verification heading had no content after
//       the commands were moved out from under it.
//   R5  A fenced block swallowed a heading, which is how the verification
//       commands came to live inside a decision.
//   R6  A cross-reference pointed at a decision number that no longer existed
//       after a renumbering.
//   R7  The file ended with more than one blank line, which git reports as
//       "new blank line at EOF" and which passed a commit unnoticed.
//   R8  The verification section held prose instead of the commands the first
//       paragraph of the document promises.
//
// The rules are checked against the real document and against eight fixtures,
// one per rule. A fixture that the rules fail to catch fails this test, so the
// test cannot silently become useless.

#include <algorithm>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "test_support.hpp"

namespace {

using Failure = std::pair<std::string, std::string>;

constexpr const char* kDocsPath = "docs/implementation-status.md";

bool startsWith(const std::string& text, const std::string& prefix)
{
    return text.size() >= prefix.size() &&
           text.compare(0, prefix.size(), prefix) == 0;
}

std::vector<std::string> splitLines(const std::string& text)
{
    std::vector<std::string> lines;
    std::istringstream stream(text);
    std::string line;
    while (std::getline(stream, line))
        lines.push_back(line);
    // A file ending in a newline yields no empty final element from getline,
    // so the trailing newline is represented explicitly. That matters for R7.
    if (!text.empty() && text.back() == '\n')
        lines.push_back(std::string());
    return lines;
}

bool isHeading(const std::string& line)
{
    return startsWith(line, "## ");
}

bool isFence(const std::string& line)
{
    const std::size_t first = line.find_first_not_of(" \t");
    return first != std::string::npos && startsWith(line.substr(first), "```");
}

// A decision opens with "N. **". Returns -1 when the line is not one, and the
// number when it is. The trailing marker matters: without it a numbered list
// inside a decision body is counted as a decision, which is exactly how R1 was
// violated.
int decisionNumber(const std::string& line)
{
    std::size_t i = 0;
    if (i >= line.size() || line[i] < '0' || line[i] > '9')
        return -1;
    long value = 0;
    while (i < line.size() && line[i] >= '0' && line[i] <= '9')
    {
        value = value * 10 + (line[i] - '0');
        ++i;
    }
    if (i >= line.size() || line[i] != '.')
        return -1;
    ++i;
    if (i >= line.size() || line[i] != ' ')
        return -1;
    ++i;
    if (i + 1 >= line.size() || line[i] != '*' || line[i + 1] != '*')
        return -1;
    return static_cast<int>(value);
}

std::vector<Failure> checkDocument(const std::string& text)
{
    const std::vector<std::string> lines = splitLines(text);
    std::vector<Failure> failures;

    std::vector<std::size_t> headings;
    for (std::size_t i = 0; i < lines.size(); ++i)
    {
        if (isHeading(lines[i]))
            headings.push_back(i);
    }

    // Decisions are counted only inside "## Open decisions". A numbered list in
    // bold is a legitimate thing for any other section to contain, and counting
    // it as a decision is precisely how the numbering was first miscounted.
    std::size_t decisionsBegin = lines.size();
    std::size_t decisionsEnd = lines.size();
    for (std::size_t h = 0; h < headings.size(); ++h)
    {
        if (lines[headings[h]] != "## Open decisions")
            continue;
        decisionsBegin = headings[h] + 1;
        decisionsEnd = (h + 1 < headings.size()) ? headings[h + 1] : lines.size();
        break;
    }

    std::vector<std::pair<std::size_t, int>> decisions;
    for (std::size_t i = decisionsBegin; i < decisionsEnd; ++i)
    {
        const int number = decisionNumber(lines[i]);
        if (number >= 0)
            decisions.emplace_back(i, number);
    }

    // A second, unrestricted scan, used only to find the span of decision-like
    // lines for R3. It cannot be section-scoped: an absorbed heading ends the
    // section, so the decisions after it would drop out of the scan and the
    // heading would no longer appear to be inside anything. That is exactly the
    // case R3 exists to catch, and the first version of this test missed it.
    std::vector<std::size_t> anyDecisionLine;
    for (std::size_t i = 0; i < lines.size(); ++i)
    {
        if (decisionNumber(lines[i]) >= 0)
            anyDecisionLine.push_back(i);
    }

    if (decisions.empty())
    {
        failures.emplace_back("R1", "no decisions found");
        return failures;
    }

    std::vector<int> numbers;
    numbers.reserve(decisions.size());
    for (const auto& entry : decisions)
        numbers.push_back(entry.second);

    // R1: strictly ascending.
    for (std::size_t i = 1; i < numbers.size(); ++i)
    {
        if (numbers[i] <= numbers[i - 1])
        {
            failures.emplace_back(
                "R1", "decision " + std::to_string(numbers[i]) +
                          " follows " + std::to_string(numbers[i - 1]) +
                          " at line " + std::to_string(decisions[i].first + 1));
            break;
        }
    }

    // R2: contiguous from one.
    for (std::size_t i = 0; i < numbers.size(); ++i)
    {
        const int want = static_cast<int>(i) + 1;
        if (numbers[i] != want)
        {
            failures.emplace_back(
                "R2", "expected decision " + std::to_string(want) + " at line " +
                          std::to_string(decisions[i].first + 1) + ", found " +
                          std::to_string(numbers[i]));
            break;
        }
    }

    // R3: no level-2 heading inside the decision span.
    for (std::size_t i = 0; i < headings.size(); ++i)
    {
        if (headings[i] > anyDecisionLine.front() &&
            headings[i] < anyDecisionLine.back())
        {
            failures.emplace_back(
                "R3", "heading \"" + lines[headings[i]] +
                          "\" sits inside the decisions at line " +
                          std::to_string(headings[i] + 1));
            break;
        }
    }

    // R4: every level-2 section has content.
    for (std::size_t h = 0; h < headings.size(); ++h)
    {
        const std::size_t end = (h + 1 < headings.size()) ? headings[h + 1]
                                                         : lines.size();
        bool content = false;
        for (std::size_t i = headings[h] + 1; i < end; ++i)
        {
            if (!lines[i].empty() && lines[i].find_first_not_of(" \t") != std::string::npos)
            {
                content = true;
                break;
            }
        }
        if (!content)
        {
            failures.emplace_back("R4", "section \"" + lines[headings[h]] +
                                            "\" is empty at line " +
                                            std::to_string(headings[h] + 1));
            break;
        }
    }

    // R5: no heading inside a fenced block.
    {
        bool fenced = false;
        for (std::size_t i = 0; i < lines.size(); ++i)
        {
            if (isFence(lines[i]))
            {
                fenced = !fenced;
                continue;
            }
            if (fenced && isHeading(lines[i]))
            {
                failures.emplace_back(
                    "R5", "heading \"" + lines[i] +
                              "\" is inside a fenced block at line " +
                              std::to_string(i + 1));
                break;
            }
        }
    }

    // R6: every "decision N" reference resolves.
    for (std::size_t i = 0; i < lines.size(); ++i)
    {
        const std::string& line = lines[i];
        std::size_t at = line.find("decision");
        while (at != std::string::npos)
        {
            std::size_t j = at + 8;
            if (j < line.size() && line[j] == 's')
                ++j;
            if (j < line.size() && line[j] == ' ')
            {
                ++j;
                std::size_t begin = j;
                while (j < line.size() && line[j] >= '0' && line[j] <= '9')
                    ++j;
                if (j > begin)
                {
                    const int referenced = std::stoi(line.substr(begin, j - begin));
                    if (std::find(numbers.begin(), numbers.end(), referenced) ==
                        numbers.end())
                    {
                        failures.emplace_back(
                            "R6", "line " + std::to_string(i + 1) +
                                      " references decision " +
                                      std::to_string(referenced) +
                                      ", which does not exist");
                    }
                }
            }
            at = line.find("decision", at + 1);
        }
    }

    // R7: at most one blank line at the end of the file.
    {
        std::size_t last = lines.size();
        while (last > 0 && lines[last - 1].empty())
            --last;
        const std::size_t trailing = lines.size() - last;
        if (trailing > 1)
        {
            failures.emplace_back(
                "R7", std::to_string(trailing) + " blank lines at end of file");
        }
    }

    // R8: the verification section holds a fenced command block.
    for (std::size_t h = 0; h < headings.size(); ++h)
    {
        // headings[h] is the line index; h is only the position in the vector.
        // Indexing lines[h] here compiled and never matched, which is how this
        // rule silently did nothing on the first run of this test.
        if (lines[headings[h]] != "## Verification commands")
            continue;
        const std::size_t end = (h + 1 < headings.size()) ? headings[h + 1]
                                                         : lines.size();
        bool fenced = false;
        for (std::size_t i = h + 1; i < end; ++i)
        {
            if (isFence(lines[i]))
            {
                fenced = true;
                break;
            }
        }
        if (!fenced)
            failures.emplace_back("R8", "the verification section has no "
                                        "fenced command block");
    }

    return failures;
}

std::string readFile(const std::string& path)
{
    std::ifstream input(path);
    if (!input)
        return std::string();
    std::ostringstream buffer;
    buffer << input.rdbuf();
    return buffer.str();
}

const std::string kGood = "# Status\n"
                          "\n"
                          "## Open decisions\n"
                          "\n"
                          "1. **First?** Yes.\n"
                          "\n"
                          "2. **Second?** No. See decision 1.\n"
                          "\n"
                          "## Verification commands\n"
                          "\n"
                          "```sh\n"
                          "cmake -S . -B build\n"
                          "```\n";

// One fixture per rule, each reduced from the defect that actually occurred.
struct Fixture
{
    const char* rule;
    const char* what;
    const char* text;
};

const Fixture kFixtures[] = {
    {"R1", "a bold numbered list counted as decisions",
     "# Status\n\n## Open decisions\n\n"
     "1. **First?** Yes.\n\n"
     "34. **Not a decision.** Items:\n\n"
     "    1. **Alpha.** Text.\n\n"
     "2. **Second?** No.\n\n"
     "## Verification commands\n\n```sh\ncmake -S . -B build\n```\n"},

    {"R2", "a decision number reused",
     "# Status\n\n## Open decisions\n\n"
     "1. **First?** Yes.\n\n"
     "1. **Also first?** No.\n\n"
     "## Verification commands\n\n```sh\ncmake -S . -B build\n```\n"},

    {"R3", "a heading inside the decision span",
     "# Status\n\n## Open decisions\n\n"
     "1. **First?** Yes.\n\n"
     "## A section that got absorbed\n\nmore.\n\n"
     "2. **Second?** No.\n\n"
     "## Verification commands\n\n```sh\ncmake -S . -B build\n```\n"},

    {"R4", "a section left empty",
     "# Status\n\n## Open decisions\n\n"
     "1. **First?** Yes.\n\n"
     "## Verification commands\n"},

    {"R5", "a fenced block that swallowed a heading",
     "# Status\n\n## Open decisions\n\n"
     "1. **First?** Yes.\n\n"
     "```sh\ncmake -S . -B build\n## Verification commands\n```\n"},

    {"R6", "a reference to a renumbered decision",
     "# Status\n\n## Open decisions\n\n"
     "1. **First?** Yes.\n\n"
     "2. **Second?** No. See decision 25.\n\n"
     "## Verification commands\n\n```sh\ncmake -S . -B build\n```\n"},

    {"R7", "blank lines at the end of the file",
     "# Status\n\n## Open decisions\n\n"
     "1. **First?** Yes.\n\n"
     "## Verification commands\n\n```sh\ncmake -S . -B build\n```\n\n\n"},

    {"R8", "prose where the commands belong",
     "# Status\n\n## Open decisions\n\n"
     "1. **First?** Yes.\n\n"
     "## Verification commands\n\n"
     "    **Updated in 6e: the chain changed.**\n"},
};

}  // namespace

int main(int argc, char** argv)
{
    const std::string root = (argc > 1) ? argv[1] : std::string(".");
    const std::string path = root + "/" + kDocsPath;
    const std::string text = readFile(path);

    GRACE_CHECK(!text.empty());

    // The rules must accept a sound document.
    const std::vector<Failure> good = checkDocument(kGood);
    GRACE_CHECK_EQ(good.size(), 0u);

    // Every fixture must be caught, and by its own rule. One check per fixture,
    // so a fixture that stops being caught changes the count rather than only
    // printing a line.
    for (const Fixture& fixture : kFixtures)
    {
        const std::vector<Failure> found = checkDocument(fixture.text);
        bool caught = false;
        for (const Failure& failure : found)
        {
            if (failure.first == fixture.rule)
                caught = true;
        }
        if (!caught)
            std::printf("  not caught: %s (%s)\n", fixture.rule, fixture.what);
        GRACE_CHECK(caught);
    }

    // The real document.
    const std::vector<Failure> failures = checkDocument(text);
    for (const Failure& failure : failures)
        std::printf("  %s: %s\n", failure.first.c_str(), failure.second.c_str());
    GRACE_CHECK_EQ(failures.size(), 0u);

    return grace::test::summary("docs_structure");
}
