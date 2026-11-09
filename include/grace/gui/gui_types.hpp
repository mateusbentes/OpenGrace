#pragma once
// The GUI module's data types: the retained controls, the input events
// and the font descriptions (docs/grace-architecture.md section 3.9).
//
// The module is plain data, deliberately: the retained tree, the hit
// test and the event dispatch are deterministic CPU work, so the same
// operations produce the same results every run. Text is UTF-8 through
// the whole contract: a control's text, an input event's text and a
// font's laid-out run all carry UTF-8 bytes, and the code points are
// decoded where the layout needs them. Nothing here names a graphics
// API or a platform.
#include <cstdint>
#include <string>
#include <vector>
namespace grace {
namespace gui {
/// The control kinds the built-in set covers: the standard controls the
/// documented architecture names. A panel is a plain container, a label
/// draws text, a button draws text and takes clicks.
enum class ControlKind : std::uint8_t
{
    Panel = 0,
    Label = 1,
    Button = 2
};
/// A rectangle in the GUI's coordinate space, y down (the screen
/// convention). x and y are the left and top edges.
struct Rect final
{
    float x = 0.0f;
    float y = 0.0f;
    float w = 0.0f;
    float h = 0.0f;
};
/// A control's description: the kind, the name, the rectangle and the
/// text. The name is unique inside one GUI; the rectangle is in the
/// GUI's coordinate space.
struct ControlDesc final
{
    std::string name;
    ControlKind kind = ControlKind::Panel;
    Rect rect;
    std::string text;
    bool visible = true;
    bool validate(std::string& message) const;
};
/// An input event's kind: a click, a key press or a text input (the
/// unicode path the documented architecture names).
enum class EventKind : std::uint8_t
{
    Click = 0,
    Key = 1,
    TextInput = 2
};
/// An input event. A click carries the position, a key carries the key
/// code, a text input carries the UTF-8 bytes the platform produced.
struct GuiEvent final
{
    EventKind kind = EventKind::Click;
    float x = 0.0f;
    float y = 0.0f;
    std::uint32_t key = 0;
    std::string text;
};
/// A font's description: the face's path (a TrueType or OpenType file)
/// and the pixel height the glyphs rasterize at. The path is the
/// adapter's concern; the null backend ignores it.
struct FontDesc final
{
    std::string path;
    float pixelHeight = 0.0f;
    bool validate(std::string& message) const;
};
/// One glyph's metrics: the code point it came from, the advance the
/// pen moves by, the bitmap's size and the offset from the pen to the
/// bitmap's top-left corner. All values are in pixels.
struct GlyphMetrics final
{
    std::uint32_t codepoint = 0;
    float advanceX = 0.0f;
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    float bitmapLeft = 0.0f;
    float bitmapTop = 0.0f;
};
/// One laid-out glyph: the metrics plus the pen position the layout
/// placed it at. The position is the bitmap's top-left corner in the
/// run's space.
struct LaidGlyph final
{
    GlyphMetrics metrics;
    float x = 0.0f;
    float y = 0.0f;
};
/// A text run laid out by the font backend: the glyphs in order and
/// the run's total advance.
struct TextLayout final
{
    std::vector<LaidGlyph> glyphs;
    float advance = 0.0f;
};
/// The GUI's statistics: the control count, the focus and the last
/// dispatched event count.
struct GuiStats final
{
    std::uint32_t controlCount = 0;
    std::uint32_t visibleCount = 0;
    std::uint64_t eventCount = 0;
};
/// Validates a control description: a non-empty name, a rectangle of
/// non-negative size. Returns false and fills the message on a
/// violation.
std::string validate(const ControlDesc& desc);
/// Validates a font description: a non-empty path, a positive pixel
/// height. Returns the empty string on success, the message on a
/// violation.
std::string validate(const FontDesc& desc);
/// Decodes the first code point of a UTF-8 string. Returns the code
/// point and sets `size` to the byte count the encoding used (1 to 4;
/// 1 and the replacement code point on an invalid lead byte).
std::uint32_t utf8Next(const std::string& text, std::size_t offset,
                       std::size_t& size);
/// The number of code points in a UTF-8 string (the replacement code
/// point counts as one).
std::size_t utf8Count(const std::string& text);
}  // namespace gui
}  // namespace grace
