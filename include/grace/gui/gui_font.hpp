#pragma once
// The GUI module's font contract: the font backend interface the
// application programs against (docs/grace-architecture.md section
// 3.9: font rendering via FreeType, unicode text rendering).
//
// The contract is the protocol reference: the null backend (14b)
// implements it with deterministic fixed metrics, and the FreeType
// adapter (14c) rasterizes the real glyphs. One class, plain data in
// and out, never throws.
#include <cstdint>
#include <string>
#include <vector>
#include "grace/gui/gui_types.hpp"
namespace grace {
namespace gui {
/// Opaque font handle. The backend owns the objects; the application
/// moves the handles.
class FontHandle;
/// The font backend contract: load faces, rasterize glyphs, lay out
/// UTF-8 text runs. Every call is safe on a null or foreign handle;
/// the misuses bump a violation counter the application can read.
class FontBackend
{
public:
    FontBackend() = default;
    virtual ~FontBackend() = default;
    FontBackend(const FontBackend&) = delete;
    FontBackend& operator=(const FontBackend&) = delete;
    FontBackend(FontBackend&&) = delete;
    FontBackend& operator=(FontBackend&&) = delete;
    /// A human-readable description of the backend, for diagnostics.
    virtual std::string describe() const = 0;
    /// The number of protocol violations since the start.
    virtual std::uint64_t violationCount() const = 0;
    /// Releases every backend resource. Idempotent.
    virtual void shutdown() = 0;
    /// Loads a font. Returns the font's id (1-based); 0 on an invalid
    /// description or a file the backend cannot read.
    virtual std::uint32_t loadFont(const FontDesc& desc) = 0;
    /// Releases a font. Releasing an unknown id is a violation.
    virtual void releaseFont(std::uint32_t fontId) = 0;
    /// The font count currently alive.
    virtual std::uint32_t fontCount() const = 0;
    /// The metrics of one code point in one font. Returns false when
    /// the font is unknown; the metrics of an unimplemented glyph are
    /// the backend's own (the null backend's are fixed).
    virtual bool glyphMetrics(std::uint32_t fontId,
                              std::uint32_t codepoint,
                              GlyphMetrics& out) = 0;
    /// Lays out a UTF-8 text run: the glyphs in order, each at its pen
    /// position, and the run's total advance.
    virtual TextLayout layout(std::uint32_t fontId,
                              const std::string& text) = 0;
};
}  // namespace gui
}  // namespace grace
