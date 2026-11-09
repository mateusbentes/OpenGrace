// The GUI module's data types: the validation of the control and font
// descriptions, and the UTF-8 decoding the layout shares.
#include "grace/gui/gui_types.hpp"
#include <map>
#include <set>
namespace grace {
namespace gui {
namespace {
/// The replacement code point the decoder emits on an invalid lead
/// byte (the Unicode replacement character).
constexpr std::uint32_t kReplacement = 0xFFFDu;
}  // namespace
std::string validate(const ControlDesc& desc)
{
    if (desc.name.empty())
    {
        return "a control's name is empty";
    }
    if (desc.rect.w < 0.0f || desc.rect.h < 0.0f)
    {
        return "a control's rectangle has a negative size";
    }
    return std::string();
}
std::string validate(const FontDesc& desc)
{
    if (desc.path.empty())
    {
        return "a font's path is empty";
    }
    if (desc.pixelHeight <= 0.0f)
    {
        return "a font's pixel height is not positive";
    }
    return std::string();
}
std::uint32_t utf8Next(const std::string& text, std::size_t offset,
                       std::size_t& size)
{
    size = 1;
    if (offset >= text.size())
    {
        return kReplacement;
    }
    const unsigned char lead =
        static_cast<unsigned char>(text[offset]);
    // The byte counts the lead byte encodes: 0xxxxxxx one, 110xxxxx
    // two, 1110xxxx three, 11110xxx four.
    std::size_t count = 1;
    std::uint32_t value = 0;
    if (lead < 0x80u)
    {
        value = lead;
    }
    else if ((lead & 0xE0u) == 0xC0u)
    {
        count = 2;
        value = lead & 0x1Fu;
    }
    else if ((lead & 0xF0u) == 0xE0u)
    {
        count = 3;
        value = lead & 0x0Fu;
    }
    else if ((lead & 0xF8u) == 0xF0u)
    {
        count = 4;
        value = lead & 0x07u;
    }
    else
    {
        return kReplacement;
    }
    if (offset + count > text.size())
    {
        return kReplacement;
    }
    for (std::size_t i = 1; i < count; ++i)
    {
        const unsigned char cont =
            static_cast<unsigned char>(text[offset + i]);
        if ((cont & 0xC0u) != 0x80u)
        {
            size = 1;
            return kReplacement;
        }
        value = (value << 6) | (cont & 0x3Fu);
    }
    size = count;
    return value;
}
std::size_t utf8Count(const std::string& text)
{
    std::size_t count = 0;
    std::size_t offset = 0;
    while (offset < text.size())
    {
        std::size_t size = 0;
        utf8Next(text, offset, size);
        offset += size;
        ++count;
    }
    return count;
}
}  // namespace gui
}  // namespace grace
