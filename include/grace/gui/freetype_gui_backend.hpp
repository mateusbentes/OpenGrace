#pragma once
// The FreeType GUI adapter's header: the font contract's real
// implementation behind GRACE_ENABLE_FREETYPE. The GUI tree part stays
// the null backend's (the tree is plain data); this class adds the
// real rasterisation on top.
#include <memory>
#include "grace/gui/null_gui_backend.hpp"
namespace grace {
namespace gui {
/// The FreeType font adapter: the font contract's execution reference.
/// It loads real faces (TrueType, OpenType), rasterizes the glyphs at
/// the pixel height, and lays out UTF-8 runs from the real metrics.
/// The GUI tree part inherits the null backend's deterministic tree.
class FreeTypeGuiBackend final : public NullGuiBackend
{
public:
    FreeTypeGuiBackend();
    ~FreeTypeGuiBackend() override;
    FreeTypeGuiBackend(const FreeTypeGuiBackend&) = delete;
    FreeTypeGuiBackend& operator=(const FreeTypeGuiBackend&) = delete;
    std::string describe() const override;
    std::uint32_t loadFont(const FontDesc& desc) override;
    void releaseFont(std::uint32_t fontId) override;
    bool glyphMetrics(std::uint32_t fontId, std::uint32_t codepoint,
                      GlyphMetrics& out) override;
    TextLayout layout(std::uint32_t fontId,
                      const std::string& text) override;
private:
    struct FtImpl;
    std::unique_ptr<FtImpl> ftImpl;
};
}  // namespace gui
}  // namespace grace
