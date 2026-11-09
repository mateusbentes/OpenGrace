#pragma once
// The null GUI backend's header: the contract with a deterministic
// retained tree and fixed font metrics. No SDK, no windowing.
#include <map>
#include <memory>
#include "grace/gui/gui_backend.hpp"
#include "grace/gui/gui_font.hpp"
namespace grace {
namespace gui {
/// The null GUI backend: the retained tree, the hit test, the focus,
/// the event dispatch and the fixed-metric font backend, all
/// deterministic. No real device is touched.
class NullGuiBackend : public GuiBackend, public FontBackend
{
public:
    NullGuiBackend();
    ~NullGuiBackend() override;
    NullGuiBackend(const NullGuiBackend&) = delete;
    NullGuiBackend& operator=(const NullGuiBackend&) = delete;
    std::string describe() const override;
    std::uint32_t capabilityFlags() const override;
    void shutdown() override;
    std::uint64_t violationCount() const override;
    std::uint32_t addControl(const ControlDesc& desc) override;
    std::uint32_t addControlUnder(std::uint32_t parentId,
                                  const ControlDesc& desc) override;
    std::uint32_t controlCount() const override;
    const ControlDesc& control(std::uint32_t controlId) const override;
    void setRect(std::uint32_t controlId, const Rect& rect) override;
    void setText(std::uint32_t controlId, const std::string& text) override;
    void setVisible(std::uint32_t controlId, bool visible) override;
    std::uint32_t hitTest(float x, float y) const override;
    void setFocus(std::uint32_t controlId) override;
    std::uint32_t focus() const override;
    std::uint32_t dispatch(const GuiEvent& event) override;
    GuiStats stats() const override;
    std::uint32_t loadFont(const FontDesc& desc) override;
    void releaseFont(std::uint32_t fontId) override;
    std::uint32_t fontCount() const override;
    bool glyphMetrics(std::uint32_t fontId, std::uint32_t codepoint,
                      GlyphMetrics& out) override;
    TextLayout layout(std::uint32_t fontId,
                      const std::string& text) override;
private:
    struct Impl;
    std::unique_ptr<Impl> impl;
};
}  // namespace gui
}  // namespace grace
