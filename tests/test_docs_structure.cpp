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
//   R9  A table row was left between two blank lines, so it was not part of
//       any table. Every renderer shows it as a paragraph of text with
//       pipes in it, and R1 to R8 do not look at tables.
//   R10 Ten paragraphs had lost their line breaks, the worst a single line of
//       688 columns. Seven of the ten were one to three columns over the limit,
//       so nothing looked wrong and nothing failed. A fence is skipped because
//       a command is meant to be long, and a table row is skipped because its
//       width follows its cells.
//
// The rules are checked against the real document and against nine fixtures,
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

// The width a reader sees, in code points rather than bytes. A UTF-8
// continuation byte is 0x80 to 0xBF and never begins a character, so the bytes
// that are not continuation bytes are exactly the code points. R10 measured
// bytes first and reported a line of 79 columns as 81, because the document
// holds an em dash: one code point, three bytes.
std::size_t displayColumns(const std::string& line)
{
    std::size_t columns = 0;
    for (std::size_t i = 0; i < line.size(); ++i)
    {
        const unsigned char c = static_cast<unsigned char>(line[i]);
        if ((c & 0xC0) != 0x80)
            ++columns;
    }
    return columns;
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

bool isTableRow(const std::string& line)
{
    const std::size_t first = line.find_first_not_of(" 	");
    if (first == std::string::npos || line[first] != '|')
        return false;
    // A shell continuation line begins with "||", which is an operator and not
    // a table row. A row never opens with two pipes: the first opens the row
    // and the second would mean an empty first cell.
    if (first + 1 < line.size() && line[first + 1] == '|')
        return false;
    return true;
}

// Counts the pipes that separate cells, so a literal pipe escaped as \| inside
// a cell does not make the row look wider than its table.
std::size_t columnCount(const std::string& line)
{
    std::size_t pipes = 0;
    for (std::size_t i = 0; i < line.size(); ++i)
    {
        if (line[i] != '|')
            continue;
        if (i > 0 && line[i - 1] == '\\')
            continue;
        ++pipes;
    }
    return pipes > 0 ? pipes - 1 : 0;
}

bool isSeparatorRow(const std::string& line)
{
    if (!isTableRow(line))
        return false;
    bool dash = false;
    for (const char c : line)
    {
        if (c == '-')
            dash = true;
        else if (c != '|' && c != ':' && c != ' ' && c != '\t')
            return false;
    }
    return dash;
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

    // R9: a contiguous run of pipe-rows must be a table. A run between two
    // blank lines is not one: Markdown wants a header, a separator and data
    // rows with no blank line inside, so a lone row renders as a paragraph of
    // text with pipes in it. A table row was left that way in this repository
    // and nothing caught it, because R1 to R8 do not look at tables.
    {
        bool fenced = false;
        std::size_t i = 0;
        while (i < lines.size())
        {
            // A fenced block holds shell, and shell has lines that begin with a
            // pipe: "|| echo ..." is an operator. R5 skips fences for headings
            // for the same reason, and this rule has to as well.
            if (isFence(lines[i]))
            {
                fenced = !fenced;
                ++i;
                continue;
            }
            if (fenced || !isTableRow(lines[i]))
            {
                ++i;
                continue;
            }
            const std::size_t start = i;
            while (i < lines.size() && isTableRow(lines[i]))
                ++i;
            const std::size_t end = i - 1;
            const std::size_t rows = end - start + 1;
            const std::size_t cols = columnCount(lines[start]);

            std::string why;
            if (rows < 3)
            {
                why = "only " + std::to_string(rows) + " row(s)";
            }
            else if (!isSeparatorRow(lines[start + 1]))
            {
                why = "no separator row under the header";
            }
            else
            {
                for (std::size_t r = start; r <= end; ++r)
                {
                    const std::size_t here = columnCount(lines[r]);
                    if (here != cols)
                    {
                        why = "line " + std::to_string(r + 1) + " has " +
                              std::to_string(here) +
                              " columns where the table has " +
                              std::to_string(cols);
                        break;
                    }
                }
            }

            if (!why.empty())
            {
                failures.emplace_back(
                    "R9", "lines " + std::to_string(start + 1) + "-" +
                              std::to_string(end + 1) +
                              " look like a table but are not one: " + why);
                break;
            }
        }
    }
    // R10: no line of prose is wider than the document's own limit. Ten
    // paragraphs had lost their line breaks, the worst at 688 columns, and no
    // rule looked at width. A fenced block holds commands, where a long line is
    // the point and wrapping it would break it, so fences are skipped. A table
    // row is as wide as its cells and Markdown has no opinion, so rows are
    // skipped too. The limit is 79 columns, which is what the document uses.
    //
    // The width is counted in code points, not bytes. Counting bytes made this
    // rule report a line of 79 columns as 81, because the document holds an em
    // dash and that character is three bytes. Both numbers were right and they
    // disagreed, which is what measuring two different things looks like.
    {
        constexpr std::size_t kLimit = 79;
        bool fenced = false;
        for (std::size_t i = 0; i < lines.size(); ++i)
        {
            if (isFence(lines[i]))
            {
                fenced = !fenced;
                continue;
            }
            if (fenced || isTableRow(lines[i]))
                continue;
            if (displayColumns(lines[i]) > kLimit)
            {
                failures.emplace_back(
                    "R10", "line " + std::to_string(i + 1) + " is " +
                               std::to_string(displayColumns(lines[i])) +
                               " columns wide, over the limit of " +
                               std::to_string(kLimit));
                break;
            }
        }
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

// kGood contains a line of 87 columns inside a fenced block. R10 skips fences,
// so the document is accepted, and the R10 fixture has no fence around its long
// line, so it is caught. If the fence skip were removed, kGood would fail and
// this test would say so.
//
// kGood also carries a line of exactly 79 code points that is 81 bytes, because
// of one em dash, U+2014. That is the case R10 got wrong when it counted bytes:
// it called a line of 79 columns 81. The first fixture could not catch that,
// because it holds no non-ASCII character and bytes and code points were the
// same number on every line of it.
const std::string kGood = "# Status\n"
                          "\n"
                          "## Open decisions\n"
                          "\n"
                          "1. **First?** Yes.\n"
                          "\n"
                          // 79 code points and 81 bytes: the em dash is one
                          // code point and three bytes in UTF-8. R10 counted
                          // bytes at first and called this line 81 columns.
                          "2. **Second?** No. See decision 1. A dash \xE2\x80\x94"
                          " here keeps the line inside the cap.\n"
                          "\n"
                          "## Verification commands\n"
                          "\n"
                          "```sh\n"
                          "cmake -S . -B build\n"
                          "grep -rn 'api' include/ \\\n"
                          "    || echo 'none'\n"
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
    {"R9", "a table row left outside its table",
     "# Status\n\n## Open decisions\n\n"
     "1. **First?** Yes.\n\n"
     "## A section\n\n"
     "| Defect | Cause | Fix |\n"
     "| --- | --- | --- |\n"
     "| one | two | three |\n\n"
     "| A row that lost its table | a patch moved it | nothing caught it |\n\n"
     "## Verification commands\n\n```sh\ncmake -S . -B build\n```\n"},
    {"R10", "a paragraph that lost its line breaks",
     "# Status\n\n## Open decisions\n\n"
     "1. **First?** Yes.\n\n"
     "2. **Second?** No. This paragraph was joined into a single line by a"
     " script that did not measure the file, and it is long enough to go past"
     " the limit of seventy-nine columns by a wide margin.\n\n"
     "## Verification commands\n\n```sh\ncmake -S . -B build\n```\n"},
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
