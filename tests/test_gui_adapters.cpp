// The GUI adapter suite (14c): the FreeType adapter against a real
// face. The suite skips (exit 77) when no system face exists, the
// same pattern the audio and script adapter suites use.
#include "contract_checks.hpp"
#include <cstdlib>
#include <string>
#include "grace/gui/freetype_gui_backend.hpp"
#include "grace/gui/gui_types.hpp"
namespace {
using grace::gui::FontDesc;
using grace::gui::FreeTypeGuiBackend;
using grace::gui::GlyphMetrics;
using grace::gui::TextLayout;
/// Finds a system face: the known path first, then the environment's
/// override. Returns the empty string when none exists.
std::string findFace()
{
    const char* override = std::getenv("GRACE_TEST_FACE");
    if (override)
    {
        return std::string(override);
    }
    return "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf";
}
}  // namespace
int main()
{
    const std::string face = findFace();
    if (face.empty())
    {
        // No system face: the suite skips, the CTest SKIP_RETURN_CODE
        // reports it.
        std::printf("gui_adapters: no system face, skipping\n");
        return 77;
    }
    FreeTypeGuiBackend backend;
    GRACE_CHECK(backend.describe() == "freetype (the real face, the "
                                      "real metrics)");
    GRACE_CHECK(backend.violationCount() == 0u);
    // The load: the real face reads, the pixel height sets.
    FontDesc desc;
    desc.path = face;
    desc.pixelHeight = 16.0f;
    const std::uint32_t font = backend.loadFont(desc);
    GRACE_CHECK(font != 0u);
    GRACE_CHECK(backend.fontCount() == 1u);
    // A nonexistent face loads nothing.
    FontDesc missing;
    missing.path = "/nonexistent/face.ttf";
    missing.pixelHeight = 16.0f;
    GRACE_CHECK(backend.loadFont(missing) == 0u);
    // The real metrics: 'A' has a positive advance and a nonzero box.
    GlyphMetrics metrics;
    GRACE_CHECK(backend.glyphMetrics(font, 'A', metrics));
    GRACE_CHECK(metrics.codepoint == 'A');
    GRACE_CHECK(metrics.advanceX > 0.0f);
    GRACE_CHECK(metrics.width > 0u);
    GRACE_CHECK(metrics.height > 0u);
    // The same face, the same code point, the same metrics: the
    // adapter is deterministic across calls.
    GlyphMetrics again;
    GRACE_CHECK(backend.glyphMetrics(font, 'A', again));
    GRACE_CHECK(again.advanceX == metrics.advanceX);
    GRACE_CHECK(again.width == metrics.width);
    GRACE_CHECK(again.height == metrics.height);
    // A missing code point (0xFFFF is unassigned) falls back to the
    // null backend's fixed metrics.
    GRACE_CHECK(backend.glyphMetrics(font, 0xFFFFu, metrics));
    GRACE_CHECK(metrics.advanceX > 0.0f);
    // The layout: the ASCII run advances by the sum of the advances.
    const TextLayout run = backend.layout(font, "AB");
    GRACE_CHECK(run.glyphs.size() == 2u);
    GRACE_CHECK(run.advance > 0.0f);
    // The UTF-8 run: "h" + U+00E9 lays out 2 glyphs.
    const TextLayout utf8 = backend.layout(font, "h\xC3\xA9");
    GRACE_CHECK(utf8.glyphs.size() == 2u);
    // The release: the face drops, an unknown release is a violation.
    backend.releaseFont(font);
    GRACE_CHECK(backend.fontCount() == 0u);
    const std::uint64_t before = backend.violationCount();
    backend.releaseFont(font);
    GRACE_CHECK(backend.violationCount() == before + 1u);
    return grace::test::summary("gui_adapters");
}
