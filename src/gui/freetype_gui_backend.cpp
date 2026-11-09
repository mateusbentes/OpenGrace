// The FreeType GUI adapter's implementation: the font contract's
// execution reference. The FreeType library initialises lazily on the
// first loadFont, each font is one FT_Face set to the pixel height,
// and the glyph metrics and the layout come from the real face. The
// rasterised bitmap's bytes are not produced here: the metrics and the
// advance are what the layout needs, and the render side samples the
// atlas a renderer builds from the same face (the adapter documents
// the split).
#include "grace/gui/freetype_gui_backend.hpp"
#include <ft2build.h>
#include FT_FREETYPE_H
namespace grace {
namespace gui {
struct FreeTypeGuiBackend::FtImpl
{
    FT_Library library = nullptr;
    bool initialised = false;
    struct Face
    {
        FT_Face face = nullptr;
        FontDesc desc;
    };
    std::map<std::uint32_t, Face> faces;
    ~FtImpl()
    {
        for (auto& entry : faces)
        {
            if (entry.second.face)
            {
                FT_Done_Face(entry.second.face);
            }
        }
        if (initialised)
        {
            FT_Done_FreeType(library);
        }
    }
    bool initialise()
    {
        if (initialised)
        {
            return true;
        }
        initialised = FT_Init_FreeType(&library) == 0;
        return initialised;
    }
};
FreeTypeGuiBackend::FreeTypeGuiBackend()
    : NullGuiBackend(), ftImpl(new FtImpl)
{
}
FreeTypeGuiBackend::~FreeTypeGuiBackend() = default;
std::string FreeTypeGuiBackend::describe() const
{
    return "freetype (the real face, the real metrics)";
}
std::uint32_t FreeTypeGuiBackend::loadFont(const FontDesc& desc)
{
    const std::string message = validate(desc);
    if (!message.empty())
    {
        return 0u;
    }
    if (!ftImpl->initialise())
    {
        return 0u;
    }
    FT_Face face = nullptr;
    if (FT_New_Face(ftImpl->library, desc.path.c_str(), 0, &face) != 0)
    {
        return 0u;
    }
    if (FT_Set_Pixel_Sizes(face, 0,
                           static_cast<FT_UInt>(desc.pixelHeight))
        != 0)
    {
        FT_Done_Face(face);
        return 0u;
    }
    const std::uint32_t id = NullGuiBackend::loadFont(desc);
    if (id == 0u)
    {
        FT_Done_Face(face);
        return 0u;
    }
    ftImpl->faces[id] = FtImpl::Face{face, desc};
    return id;
}
void FreeTypeGuiBackend::releaseFont(std::uint32_t fontId)
{
    const auto it = ftImpl->faces.find(fontId);
    if (it != ftImpl->faces.end())
    {
        if (it->second.face)
        {
            FT_Done_Face(it->second.face);
        }
        ftImpl->faces.erase(it);
    }
    NullGuiBackend::releaseFont(fontId);
}
bool FreeTypeGuiBackend::glyphMetrics(std::uint32_t fontId,
                                      std::uint32_t codepoint,
                                      GlyphMetrics& out)
{
    const auto it = ftImpl->faces.find(fontId);
    if (it == ftImpl->faces.end())
    {
        return NullGuiBackend::glyphMetrics(fontId, codepoint, out);
    }
    FT_Face face = it->second.face;
    if (FT_Get_Char_Index(face, codepoint) == 0
        || FT_Load_Glyph(face, 0, FT_LOAD_NO_BITMAP) != 0)
    {
        // The missing glyph falls back to the null backend's fixed
        // metrics so the layout still advances deterministically.
        return NullGuiBackend::glyphMetrics(fontId, codepoint, out);
    }
    const FT_GlyphSlot slot = face->glyph;
    const FT_Glyph_Metrics& m = slot->metrics;
    const float scale = 1.0f / 64.0f;
    const float widthPx = static_cast<float>(m.width) * scale;
    const float heightPx = static_cast<float>(m.height) * scale;
    out.codepoint = codepoint;
    out.advanceX = static_cast<float>(m.horiAdvance) * scale;
    out.width = static_cast<std::uint32_t>(widthPx);
    out.height = static_cast<std::uint32_t>(heightPx);
    out.bitmapLeft = static_cast<float>(slot->bitmap_left);
    out.bitmapTop = static_cast<float>(slot->bitmap_top);
    return true;
}
TextLayout FreeTypeGuiBackend::layout(std::uint32_t fontId,
                                      const std::string& text)
{
    const auto it = ftImpl->faces.find(fontId);
    if (it == ftImpl->faces.end())
    {
        return NullGuiBackend::layout(fontId, text);
    }
    TextLayout out;
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
