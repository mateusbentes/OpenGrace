#pragma once
// The GUI module's contract: the backend interface the application
// programs against (docs/grace-architecture.md section 3.9). One class,
// plain data in and out, never throws.
//
// The contract is the protocol reference for every backend: the null
// backend (14b) implements it with a deterministic retained tree, and
// the FreeType adapter (14c) rasterizes the font backend's glyphs. The
// unit of structure is a control; the unit of text is the UTF-8 string.
#include <cstdint>
#include <string>
#include "grace/gui/gui_types.hpp"
namespace grace {
namespace gui {
/// Opaque font handle. The backend owns the objects; the application
/// moves the handles.
class GuiFont;
/// Opaque text handle. The backend owns the objects; the application
/// moves the handles.
class GuiText;
/// The GUI backend contract: the retained tree of controls, the hit
/// test and focus, the event dispatch and the text queries. Every call
/// is safe on a null or foreign handle; the misuses bump a violation
/// counter the application can read.
class GuiBackend
{
public:
    GuiBackend() = default;
    virtual ~GuiBackend() = default;
    GuiBackend(const GuiBackend&) = delete;
    GuiBackend& operator=(const GuiBackend&) = delete;
    GuiBackend(GuiBackend&&) = delete;
    GuiBackend& operator=(GuiBackend&&) = delete;
    /// A human-readable description of the backend, for diagnostics.
    virtual std::string describe() const = 0;
    /// The backend capability flags: 0 means the plain contract. A
    /// backend may set bits for extra features (3D GUI systems, for
    /// example).
    virtual std::uint32_t capabilityFlags() const = 0;
    /// Releases every backend resource. Idempotent.
    virtual void shutdown() = 0;
    /// The number of protocol violations since the start: the misuses
    /// the application can detect (a foreign handle, an unknown
    /// control).
    virtual std::uint64_t violationCount() const = 0;
    // -- Controls ---------------------------------------
    //
    // A control is a node in the retained tree: the kind, the name,
    // the rectangle and the text. The tree is flat with a parent index
    // (the root's parent is 0), so the serialisation stays plain data.
    /// Adds a control to the root. Returns the control's id (1-based);
    /// 0 on an invalid description or a duplicate name.
    virtual std::uint32_t addControl(const ControlDesc& desc) = 0;
    /// Adds a control under a parent. Returns the control's id;
    /// 0 on an unknown parent.
    virtual std::uint32_t addControlUnder(std::uint32_t parentId,
                                          const ControlDesc& desc) = 0;
    /// The control count.
    virtual std::uint32_t controlCount() const = 0;
    /// The control by id (the order the controls were added).
    virtual const ControlDesc& control(std::uint32_t controlId) const = 0;
    /// Sets a control's rectangle.
    virtual void setRect(std::uint32_t controlId, const Rect& rect) = 0;
    /// Sets a control's text.
    virtual void setText(std::uint32_t controlId,
                         const std::string& text) = 0;
    /// Sets a control's visibility.
    virtual void setVisible(std::uint32_t controlId, bool visible) = 0;
    /// The control whose rectangle contains the point, the topmost
    /// visible one (the last added on a tie). Returns 0 when the point
    /// hits no control.
    virtual std::uint32_t hitTest(float x, float y) const = 0;
    /// Sets the focus to a control. The focus is the input's target.
    virtual void setFocus(std::uint32_t controlId) = 0;
    /// The focused control's id; 0 when nothing has the focus.
    virtual std::uint32_t focus() const = 0;
    /// Dispatches one input event: a click dispatches to the hit
    /// control, a key or a text input to the focused control. Returns
    /// the control that received the event; 0 when none did.
    virtual std::uint32_t dispatch(const GuiEvent& event) = 0;
    /// The GUI's statistics.
    virtual GuiStats stats() const = 0;
};
}  // namespace gui
}  // namespace grace
