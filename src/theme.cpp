#include "theme.h"

namespace theme {
// One typeface everywhere. The panel, card and history window draw text with GDI, which snaps letters to
// whole pixels; Segoe UI is built for that and stays even at every size, while Noto Sans (made for smooth
// renderers like F1DockTimers' Qt) came out with uneven strokes and gaps inside words at small sizes, and
// mixing the two per string or per size looked inconsistent. Segoe UI also covers every UI language, and
// it ships with Windows, so the exe carries no font of its own.
const wchar_t* Family(const std::wstring&) { return L"Segoe UI"; }

COLORREF Mix(COLORREF a, COLORREF b, float t) {
    auto ch = [t](int x, int y) { return (BYTE)(x + (y - x) * t + 0.5f); };
    return RGB(ch(GetRValue(a), GetRValue(b)), ch(GetGValue(a), GetGValue(b)), ch(GetBValue(a), GetBValue(b)));
}

}  // namespace theme
