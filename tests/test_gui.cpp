// The GUI suite: the contract (14a) and the null backend (14b) with
// the deterministic retained tree and the fixed-metric font backend.
// Every check runs against the null backend, which is reproducible
// bit-for-bit. The FreeType adapter lives in the adapter suite (14c),
// which the SDK option builds.
#include "contract_checks.hpp"
#include <string>
#include "grace/gui/gui_types.hpp"
#include "grace/gui/null_gui_backend.hpp"
namespace {
using grace::gui::ControlDesc;
using grace::gui::ControlKind;
using grace::gui::FontDesc;
using grace::gui::GlyphMetrics;
using grace::gui::EventKind;
using grace::gui::GuiEvent;
using grace::gui::GuiStats;
using grace::gui::NullGuiBackend;
using grace::gui::Rect;
using grace::gui::TextLayout;
ControlDesc panel(const std::string& name, const Rect& rect)
{
    ControlDesc desc;
    desc.name = name;
    desc.kind = ControlKind::Panel;
    desc.rect = rect;
    return desc;
}
void testValidation()
{
    ControlDesc desc = panel("root", Rect{0.0f, 0.0f, 10.0f, 10.0f});
    GRACE_CHECK(grace::gui::validate(desc).empty());
    desc.name.clear();
    GRACE_CHECK(grace::gui::validate(desc)
                == "a control's name is empty");
    desc = panel("root", Rect{0.0f, 0.0f, -1.0f, 10.0f});
    GRACE_CHECK(grace::gui::validate(desc)
                == "a control's rectangle has a negative size");
    FontDesc font;
    font.path = "/some/face.ttf";
    font.pixelHeight = 16.0f;
    GRACE_CHECK(grace::gui::validate(font).empty());
    font.path.clear();
    GRACE_CHECK(grace::gui::validate(font) == "a font's path is empty");
    font.path = "/some/face.ttf";
    font.pixelHeight = 0.0f;
    GRACE_CHECK(grace::gui::validate(font)
                == "a font's pixel height is not positive");
}
void testUtf8()
{
    // ASCII: one byte per code point.
    std::size_t size = 0;
    GRACE_CHECK(grace::gui::utf8Next("hi", 0u, size) == 'h');
    GRACE_CHECK(size == 1u);
    // The two-byte form: U+00E9 (é) encodes as C3 A9.
    GRACE_CHECK(grace::gui::utf8Next("\xC3\xA9", 0u, size) == 0xE9u);
    GRACE_CHECK(size == 2u);
    // The three-byte form: U+2603 (☃) encodes as E2 98 83.
    GRACE_CHECK(grace::gui::utf8Next("\xE2\x98\x83", 0u, size)
                == 0x2603u);
    GRACE_CHECK(size == 3u);
    // The four-byte form: U+1F600 encodes as F0 9F 98 80.
    GRACE_CHECK(grace::gui::utf8Next("\xF0\x9F\x98\x80", 0u, size)
                == 0x1F600u);
    GRACE_CHECK(size == 4u);
    // An invalid lead byte and a truncated sequence both decode to the
    // replacement code point.
    GRACE_CHECK(grace::gui::utf8Next("\xFF", 0u, size) == 0xFFFDu);
    GRACE_CHECK(grace::gui::utf8Next("\xC3", 0u, size) == 0xFFFDu);
    // The counts: "héli" is 5 bytes, 4 code points.
    GRACE_CHECK(grace::gui::utf8Count("h\xC3\xA9li") == 4u);
    GRACE_CHECK(grace::gui::utf8Count("") == 0u);
}
void testBackendContract()
{
    NullGuiBackend backend;
    GRACE_CHECK(backend.describe()
                == "null (headless, deterministic tree, no device)");
    GRACE_CHECK(backend.capabilityFlags() == 0u);
    GRACE_CHECK(backend.violationCount() == 0u);
    GRACE_CHECK(backend.controlCount() == 0u);
    GRACE_CHECK(backend.fontCount() == 0u);
    GRACE_CHECK(backend.focus() == 0u);
    const GuiStats stats = backend.stats();
    GRACE_CHECK(stats.controlCount == 0u);
    GRACE_CHECK(stats.visibleCount == 0u);
    GRACE_CHECK(stats.eventCount == 0u);
}
void testControlLifecycle()
{
    NullGuiBackend backend;
    // An invalid description returns 0 and bumps the violation counter.
    GRACE_CHECK(backend.addControl(panel("", Rect{})) == 0u);
    GRACE_CHECK(backend.violationCount() == 1u);
    const std::uint32_t root =
        backend.addControl(panel("root", Rect{0.0f, 0.0f, 100.0f, 50.0f}));
    GRACE_CHECK(root == 1u);
    GRACE_CHECK(backend.controlCount() == 1u);
    // A duplicate name returns 0.
    GRACE_CHECK(backend.addControl(panel("root", Rect{})) == 0u);
    GRACE_CHECK(backend.violationCount() == 2u);
    // A child under an unknown parent returns 0.
    GRACE_CHECK(backend.addControlUnder(99u,
                                        panel("child", Rect{}))
                == 0u);
    GRACE_CHECK(backend.violationCount() == 3u);
    const std::uint32_t child =
        backend.addControlUnder(root, panel("child",
                                            Rect{10.0f, 10.0f,
                                                 20.0f, 10.0f}));
    GRACE_CHECK(child == 2u);
    GRACE_CHECK(backend.controlCount() == 2u);
    // The text and the rectangle mutate in place.
    backend.setText(child, "click me");
    GRACE_CHECK(backend.control(child).text == "click me");
    backend.setRect(child, Rect{10.0f, 10.0f, 30.0f, 10.0f});
    GRACE_CHECK(backend.control(child).rect.w == 30.0f);
    backend.setVisible(child, false);
    GRACE_CHECK(!backend.control(child).visible);
    // An unknown control id is a violation and reads the empty
    // description.
    GRACE_CHECK(backend.control(99u).name.empty());
    GRACE_CHECK(backend.violationCount() == 4u);
    // The shutdown is idempotent and clears the tree.
    backend.shutdown();
    backend.shutdown();
    GRACE_CHECK(backend.controlCount() == 0u);
}
void testHitTest()
{
    NullGuiBackend backend;
    const std::uint32_t a =
        backend.addControl(panel("a", Rect{0.0f, 0.0f, 10.0f, 10.0f}));
    const std::uint32_t b =
        backend.addControl(panel("b", Rect{5.0f, 5.0f, 10.0f, 10.0f}));
    // A point inside both picks the topmost (the last added).
    GRACE_CHECK(backend.hitTest(6.0f, 6.0f) == b);
    GRACE_CHECK(backend.hitTest(1.0f, 1.0f) == a);
    GRACE_CHECK(backend.hitTest(20.0f, 20.0f) == 0u);
    // An invisible control never hits.
    backend.setVisible(b, false);
    GRACE_CHECK(backend.hitTest(6.0f, 6.0f) == a);
    backend.setVisible(b, true);
    GRACE_CHECK(backend.hitTest(6.0f, 6.0f) == b);
}
void testFocusAndDispatch()
{
    NullGuiBackend backend;
    const std::uint32_t a =
        backend.addControl(panel("a", Rect{0.0f, 0.0f, 10.0f, 10.0f}));
    const std::uint32_t label =
        backend.addControl(panel("label", Rect{20.0f, 0.0f, 10.0f,
                                               10.0f}));
    GRACE_CHECK(backend.focus() == 0u);
    backend.setFocus(label);
    GRACE_CHECK(backend.focus() == label);
    // An unknown focus target is a violation.
    backend.setFocus(99u);
    GRACE_CHECK(backend.violationCount() == 1u);
    GRACE_CHECK(backend.focus() == label);
    // A click dispatches to the hit control.
    GuiEvent click;
    click.kind = EventKind::Click;
    click.x = 1.0f;
    click.y = 1.0f;
    GRACE_CHECK(backend.dispatch(click) == a);
    // A key and a text input dispatch to the focused control.
    GuiEvent key;
    key.kind = EventKind::Key;
    key.key = 65u;
    GRACE_CHECK(backend.dispatch(key) == label);
    GuiEvent text;
    text.kind = EventKind::TextInput;
    text.text = "h\xC3\xA9llo";
    GRACE_CHECK(backend.dispatch(text) == label);
    // The event count covers every dispatch.
    GRACE_CHECK(backend.stats().eventCount == 3u);
    // A click on empty space dispatches to none.
    click.x = 50.0f;
    click.y = 50.0f;
    GRACE_CHECK(backend.dispatch(click) == 0u);
    GRACE_CHECK(backend.stats().eventCount == 4u);
}
void testFontContract()
{
    NullGuiBackend backend;
    // An invalid font description returns 0.
    FontDesc bad;
    bad.pixelHeight = 16.0f;
    GRACE_CHECK(backend.loadFont(bad) == 0u);
    GRACE_CHECK(backend.violationCount() == 1u);
    FontDesc desc;
    desc.path = "any";
    desc.pixelHeight = 16.0f;
    const std::uint32_t font = backend.loadFont(desc);
    GRACE_CHECK(font == 1u);
    GRACE_CHECK(backend.fontCount() == 1u);
    // The fixed metrics vary with the code point's low bits.
    GlyphMetrics metrics;
    GRACE_CHECK(backend.glyphMetrics(font, 'a', metrics));
    GRACE_CHECK(metrics.codepoint == 'a');
    GRACE_CHECK(metrics.advanceX == 8.0f + static_cast<float>('a' % 4u));
    GRACE_CHECK(metrics.width == 16u);
    GRACE_CHECK(metrics.height == 16u);
    // An unknown font is a violation.
    GRACE_CHECK(!backend.glyphMetrics(99u, 'a', metrics));
    GRACE_CHECK(backend.violationCount() == 2u);
    // The release and the unknown release.
    backend.releaseFont(99u);
    GRACE_CHECK(backend.violationCount() == 3u);
    backend.releaseFont(font);
    GRACE_CHECK(backend.fontCount() == 0u);
}
void testLayout()
{
    NullGuiBackend backend;
    FontDesc desc;
    desc.path = "any";
    desc.pixelHeight = 16.0f;
    const std::uint32_t font = backend.loadFont(desc);
    // The ASCII run: one glyph per byte, the pen advances by the
    // glyph's advance.
    const TextLayout ascii = backend.layout(font, "ab");
    GRACE_CHECK(ascii.glyphs.size() == 2u);
    GRACE_CHECK(ascii.glyphs[0].metrics.codepoint == 'a');
    const float advanceA =
        8.0f + static_cast<float>('a' % 4u);
    const float advanceB =
        8.0f + static_cast<float>('b' % 4u);
    GRACE_CHECK(ascii.glyphs[0].x == 0.0f);
    GRACE_CHECK(ascii.glyphs[1].x == advanceA);
    GRACE_CHECK(ascii.advance == advanceA + advanceB);
    // The UTF-8 run: "hé" is 3 bytes, 2 code points.
    const TextLayout utf8 = backend.layout(font, "h\xC3\xA9");
    GRACE_CHECK(utf8.glyphs.size() == 2u);
    GRACE_CHECK(utf8.glyphs[1].metrics.codepoint == 0xE9u);
    // An unknown font returns an empty layout and a violation.
    GRACE_CHECK(backend.layout(99u, "x").glyphs.empty());
    GRACE_CHECK(backend.violationCount() == 1u);
    // An empty string lays out nothing.
    GRACE_CHECK(backend.layout(font, "").glyphs.empty());
    GRACE_CHECK(backend.layout(font, "").advance == 0.0f);
}
}  // namespace
int main()
{
    testValidation();
    testUtf8();
    testBackendContract();
    testControlLifecycle();
    testHitTest();
    testFocusAndDispatch();
    testFontContract();
    testLayout();
    return grace::test::summary("gui");
}
