// The null GUI backend's implementation: the contract with a
// deterministic retained tree and fixed font metrics. No real device
// is touched: the hit test walks the rectangles, the dispatch records
// the event counts, and the font backend's metrics are fixed per code
// point, so every run is reproducible bit-for-bit.
#include "grace/gui/null_gui_backend.hpp"
#include <algorithm>
#include <cmath>
#include <set>
namespace grace {
namespace gui {
namespace {
/// The null font's fixed metrics: the advance and the bitmap box the
/// code point maps to. The advance varies with the code point's low
/// bits (a deterministic stand-in for the real face's metrics), the
/// box is the pixel height scaled.
constexpr float kBaseAdvance = 8.0f;
}  // namespace
struct NullGuiBackend::Impl
{
    struct Control
    {
        ControlDesc desc;
        std::uint32_t parent = 0;
    };
    struct Font
    {
        FontDesc desc;
    };
    std::map<std::uint32_t, Control> controls;
    std::map<std::uint32_t, Font> fonts;
    std::uint32_t nextControlId = 1;
    std::uint32_t nextFontId = 1;
    std::uint32_t focusId = 0;
    std::uint64_t violations = 0;
    std::uint64_t eventCount = 0;
    bool shutdown = false;
    std::set<std::string> names;
};
NullGuiBackend::NullGuiBackend() : impl(new Impl)
{
}
NullGuiBackend::~NullGuiBackend() = default;
std::string NullGuiBackend::describe() const
{
    return "null (headless, deterministic tree, no device)";
}
std::uint32_t NullGuiBackend::capabilityFlags() const
{
    return 0u;
}
void NullGuiBackend::shutdown()
{
    impl->controls.clear();
    impl->fonts.clear();
    impl->names.clear();
    impl->focusId = 0;
    impl->shutdown = true;
}
std::uint64_t NullGuiBackend::violationCount() const
{
    return impl->violations;
}
std::uint32_t NullGuiBackend::addControl(const ControlDesc& desc)
{
    if (!validate(desc).empty() || impl->names.count(desc.name))
    {
        ++impl->violations;
        return 0u;
    }
    const std::uint32_t id = impl->nextControlId++;
    impl->controls[id] = Impl::Control{desc, 0u};
    impl->names.insert(desc.name);
    return id;
}
std::uint32_t NullGuiBackend::addControlUnder(std::uint32_t parentId,
                                              const ControlDesc& desc)
{
    if (parentId != 0u && !impl->controls.count(parentId))
    {
        ++impl->violations;
        return 0u;
    }
    const std::uint32_t id = addControl(desc);
    if (id != 0u)
    {
        impl->controls[id].parent = parentId;
    }
    return id;
}
std::uint32_t NullGuiBackend::controlCount() const
{
    return static_cast<std::uint32_t>(impl->controls.size());
}
const ControlDesc& NullGuiBackend::control(std::uint32_t controlId) const
{
    static const ControlDesc empty;
    const auto it = impl->controls.find(controlId);
    if (it == impl->controls.end())
    {
        ++impl->violations;
        return empty;
    }
    return it->second.desc;
}
void NullGuiBackend::setRect(std::uint32_t controlId, const Rect& rect)
{
    const auto it = impl->controls.find(controlId);
    if (it == impl->controls.end())
    {
        ++impl->violations;
        return;
    }
    it->second.desc.rect = rect;
}
void NullGuiBackend::setText(std::uint32_t controlId,
                             const std::string& text)
{
    const auto it = impl->controls.find(controlId);
    if (it == impl->controls.end())
    {
        ++impl->violations;
        return;
    }
    it->second.desc.text = text;
}
void NullGuiBackend::setVisible(std::uint32_t controlId, bool visible)
{
    const auto it = impl->controls.find(controlId);
    if (it == impl->controls.end())
    {
        ++impl->violations;
        return;
    }
    it->second.desc.visible = visible;
}
std::uint32_t NullGuiBackend::hitTest(float x, float y) const
{
    // The topmost visible control whose rectangle contains the point:
    // the last added wins on a tie, which the ascending id order gives.
    for (auto it = impl->controls.rbegin(); it != impl->controls.rend();
         ++it)
    {
        const ControlDesc& desc = it->second.desc;
        if (!desc.visible)
        {
            continue;
        }
        if (x >= desc.rect.x && x < desc.rect.x + desc.rect.w
            && y >= desc.rect.y && y < desc.rect.y + desc.rect.h)
        {
            return it->first;
        }
    }
    return 0u;
}
void NullGuiBackend::setFocus(std::uint32_t controlId)
{
    if (controlId != 0u && !impl->controls.count(controlId))
    {
        ++impl->violations;
        return;
    }
    impl->focusId = controlId;
}
std::uint32_t NullGuiBackend::focus() const
{
    return impl->focusId;
}
std::uint32_t NullGuiBackend::dispatch(const GuiEvent& event)
{
    ++impl->eventCount;
    if (event.kind == EventKind::Click)
    {
        const std::uint32_t hit = hitTest(event.x, event.y);
        return hit;
    }
    // A key or a text input goes to the focused control.
    return impl->focusId;
}
GuiStats NullGuiBackend::stats() const
{
    GuiStats stats;
    stats.controlCount =
        static_cast<std::uint32_t>(impl->controls.size());
    for (const auto& entry : impl->controls)
    {
        if (entry.second.desc.visible)
        {
            ++stats.visibleCount;
        }
    }
    stats.eventCount = impl->eventCount;
    return stats;
}
std::uint32_t NullGuiBackend::loadFont(const FontDesc& desc)
{
    if (!validate(desc).empty())
    {
        ++impl->violations;
        return 0u;
    }
    const std::uint32_t id = impl->nextFontId++;
    impl->fonts[id] = Impl::Font{desc};
    return id;
}
void NullGuiBackend::releaseFont(std::uint32_t fontId)
{
    if (!impl->fonts.erase(fontId))
    {
        ++impl->violations;
    }
}
std::uint32_t NullGuiBackend::fontCount() const
{
    return static_cast<std::uint32_t>(impl->fonts.size());
}
bool NullGuiBackend::glyphMetrics(std::uint32_t fontId,
                                  std::uint32_t codepoint,
                                  GlyphMetrics& out)
{
    const auto it = impl->fonts.find(fontId);
    if (it == impl->fonts.end())
    {
        ++impl->violations;
        return false;
    }
    // The fixed metrics: the advance varies with the code point's low
    // bits (a deterministic stand-in for the real face's advance
    // widths), the box is the pixel height scaled, the offset is 0.
    const FontDesc& desc = it->second.desc;
    out.codepoint = codepoint;
    out.advanceX = kBaseAdvance
                   + static_cast<float>(codepoint % 4u);
    out.width = static_cast<std::uint32_t>(desc.pixelHeight);
    out.height = static_cast<std::uint32_t>(desc.pixelHeight);
    out.bitmapLeft = 0.0f;
    out.bitmapTop = 0.0f;
    return true;
}
TextLayout NullGuiBackend::layout(std::uint32_t fontId,
                                  const std::string& text)
{
    TextLayout out;
    const auto it = impl->fonts.find(fontId);
    if (it == impl->fonts.end())
    {
        ++impl->violations;
        return out;
    }
    float penX = 0.0f;
    std::size_t offset = 0;
    while (offset < text.size())
    {
        std::size_t size = 0;
        const std::uint32_t cp = utf8Next(text, offset, size);
        GlyphMetrics metrics;
        if (glyphMetrics(fontId, cp, metrics))
        {
            LaidGlyph glyph;
            glyph.metrics = metrics;
            glyph.x = penX + metrics.bitmapLeft;
            glyph.y = metrics.bitmapTop;
            out.glyphs.push_back(glyph);
            penX += metrics.advanceX;
        }
        offset += size;
    }
    out.advance = penX;
    return out;
}
}  // namespace gui
}  // namespace grace
