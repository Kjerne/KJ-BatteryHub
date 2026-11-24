// The panel (see panel.h). Everything is drawn into one 32-bit bitmap: GDI for the text, a small
// anti-aliased rasterizer below for the rounded shapes, lines and icons, then UpdateLayeredWindow for
// the rounded, see-through corners. The bitmap and the fonts are freed when the panel closes.
#include "panel.h"
#include "app_identity.h"

#include "battery_history.h"
#include "i18n.h"
#include "memory_trim.h"
#include "theme.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <ctime>
#include <map>
#include <tuple>

namespace panel {
namespace {

using namespace theme;
using battery_history::RuntimeEstimate;
using battery_history::RuntimeState;

constexpr int kBaseW = 500, kBaseH = 640, kGap = 10;
const wchar_t kClass[] = L"BatteryHubPanel";

Host g_host;
HINSTANCE g_inst = nullptr;
HWND g_wnd = nullptr;
float g_s = 1.0f;  // DPI scale
POINT g_pos{};
int g_w = 0, g_h = 0;

// ---- what is shown -----------------------------------------------------------------------------
Tab g_tab = Tab::Devices;
std::wstring g_histKey;
int g_period = 1;  // 24 hours, 7 days, 30 days, 1 year, all time
int g_page = 0;    // settings page
int g_scroll[3] = {};
int g_contentH[3] = {};
bool g_overflow[3] = {};
int g_open = -1;        // settings item whose list is open (kDeviceList: the History tab's device field)
constexpr int kDeviceList = 1 << 20;
constexpr int kDeviceActions = kDeviceList + 1;  // the actions of g_actionsKey, at g_actionsAt
std::wstring g_actionsKey;
std::vector<Choice> g_actions;
RECT g_actionsAt{};
int g_dropScroll = 0;   // first item shown in that list
bool g_pinned = false;
bool g_busy = false;    // a dialog or menu of ours is open: do not close on deactivation
ULONGLONG g_hiddenAt = 0;
std::vector<Device> g_devs;
std::vector<std::wstring> g_unsupported;
std::vector<Setting> g_sets;
RECT g_body{};

enum class H {
    None, Tab, Pin, Gear, Refresh, Device, HistDevice, Period, Page, Toggle, Select, Button, Drop,
    Clear, Custom, Window, Support, Thumb, Actions, Chart
};
struct Hit {
    RECT r{};
    H kind = H::None;
    int index = -1;
};
std::vector<Hit> g_hits;
Hit g_hover, g_press;
int g_thumbGrab = -1;  // scrollbar drag: mouse y minus thumb top

bool Same(const Hit& a, const Hit& b) { return a.kind == b.kind && a.index == b.index; }
bool Hovered(H kind, int index = -1) { return g_hover.kind == kind && g_hover.index == index; }

// ---- canvas and rasterizer ---------------------------------------------------------------------
struct RectF { float l, t, r, b; };
struct PtF { float x, y; };

struct Canvas {
    HDC dc = nullptr;
    HBITMAP bmp = nullptr;
    HGDIOBJ old = nullptr;
    uint32_t* px = nullptr;
    int w = 0, h = 0;
    RECT clip{};
    bool Resize(int W, int H) {
        if (dc && W == w && H == h) return true;
        Free();
        BITMAPINFO bi{};
        bi.bmiHeader.biSize = sizeof(bi.bmiHeader);
        bi.bmiHeader.biWidth = W;
        bi.bmiHeader.biHeight = -H;  // top-down
        bi.bmiHeader.biPlanes = 1;
        bi.bmiHeader.biBitCount = 32;
        bi.bmiHeader.biCompression = BI_RGB;
        dc = CreateCompatibleDC(nullptr);
        void* bits = nullptr;
        bmp = dc ? CreateDIBSection(dc, &bi, DIB_RGB_COLORS, &bits, nullptr, 0) : nullptr;
        if (!bmp) { Free(); return false; }
        old = SelectObject(dc, bmp);
        px = static_cast<uint32_t*>(bits);
        w = W;
        h = H;
        clip = { 0, 0, W, H };
        SetBkMode(dc, TRANSPARENT);
        return true;
    }
    void Free() {
        if (dc && old) SelectObject(dc, old);
        if (bmp) DeleteObject(bmp);
        if (dc) DeleteDC(dc);
        dc = nullptr; bmp = nullptr; old = nullptr; px = nullptr; w = h = 0;
    }
};
Canvas g_cv;

int P(float v) { return static_cast<int>(std::lround(v * g_s)); }
float F(float v) { return v * g_s; }

void SetClip(const RECT& r) {
    g_cv.clip = { std::max(0L, r.left), std::max(0L, r.top), std::min<LONG>(g_cv.w, r.right), std::min<LONG>(g_cv.h, r.bottom) };
    HRGN rgn = CreateRectRgn(g_cv.clip.left, g_cv.clip.top, g_cv.clip.right, g_cv.clip.bottom);
    SelectClipRgn(g_cv.dc, rgn);
    DeleteObject(rgn);
}
void NoClip() {
    g_cv.clip = { 0, 0, g_cv.w, g_cv.h };
    SelectClipRgn(g_cv.dc, nullptr);
}

inline void Blend(int x, int y, COLORREF c, float a) {
    if (a <= 0.003f || x < g_cv.clip.left || y < g_cv.clip.top || x >= g_cv.clip.right || y >= g_cv.clip.bottom) return;
    uint32_t& d = g_cv.px[y * g_cv.w + x];
    const float sr = GetRValue(c), sg = GetGValue(c), sb = GetBValue(c);
    if (a >= 0.997f) {
        d = 0xFF000000u | (static_cast<uint32_t>(sr) << 16) | (static_cast<uint32_t>(sg) << 8) | static_cast<uint32_t>(sb);
        return;
    }
    const float dr = (d >> 16) & 255, dg = (d >> 8) & 255, db = d & 255;
    const auto ch = [a](float s, float t) { return static_cast<uint32_t>(t + (s - t) * a + 0.5f); };
    d = 0xFF000000u | (ch(sr, dr) << 16) | (ch(sg, dg) << 8) | ch(sb, db);
}

float Clamp01(float v) { return v < 0 ? 0 : v > 1 ? 1 : v; }

// Coverage of the pixel centred on (x, y) by a rounded rectangle (signed distance, 1 px soft edge).
float RoundCov(float x, float y, const RectF& r, float rad) {
    const float hx = (r.r - r.l) / 2, hy = (r.b - r.t) / 2;
    if (hx <= 0 || hy <= 0) return 0;
    rad = std::max(0.0f, std::min(rad, std::min(hx, hy)));
    const float qx = std::fabs(x - (r.l + r.r) / 2) - (hx - rad), qy = std::fabs(y - (r.t + r.b) / 2) - (hy - rad);
    const float ox = std::max(qx, 0.0f), oy = std::max(qy, 0.0f);
    const float d = std::sqrt(ox * ox + oy * oy) + std::min(std::max(qx, qy), 0.0f) - rad;
    return Clamp01(0.5f - d);
}

void FillRound(const RectF& r, float rad, COLORREF c, float a = 1) {
    GdiFlush();
    const int x0 = std::max<int>(g_cv.clip.left, static_cast<int>(std::floor(r.l))),
              x1 = std::min<int>(g_cv.clip.right, static_cast<int>(std::ceil(r.r)));
    const int y0 = std::max<int>(g_cv.clip.top, static_cast<int>(std::floor(r.t))),
              y1 = std::min<int>(g_cv.clip.bottom, static_cast<int>(std::ceil(r.b)));
    for (int y = y0; y < y1; ++y)
        for (int x = x0; x < x1; ++x) Blend(x, y, c, RoundCov(x + 0.5f, y + 0.5f, r, rad) * a);
}

void StrokeRound(const RectF& r, float rad, float width, COLORREF c, float a = 1) {
    GdiFlush();
    const RectF in{ r.l + width, r.t + width, r.r - width, r.b - width };
    const int x0 = std::max<int>(g_cv.clip.left, static_cast<int>(std::floor(r.l))),
              x1 = std::min<int>(g_cv.clip.right, static_cast<int>(std::ceil(r.r)));
    const int y0 = std::max<int>(g_cv.clip.top, static_cast<int>(std::floor(r.t))),
              y1 = std::min<int>(g_cv.clip.bottom, static_cast<int>(std::ceil(r.b)));
    const float inner = std::max(0.0f, rad - width);
    for (int y = y0; y < y1; ++y)
        for (int x = x0; x < x1; ++x) {
            const float px = x + 0.5f, py = y + 0.5f;
            const float cov = RoundCov(px, py, r, rad) - RoundCov(px, py, in, inner);
            Blend(x, y, c, Clamp01(cov) * a);
        }
}

void FillRect(const RECT& r, COLORREF c, float a = 1) {
    FillRound(RectF{ (float)r.left, (float)r.top, (float)r.right, (float)r.bottom }, 0, c, a);
}

void Dot(float cx, float cy, float rad, COLORREF c, float a = 1) {
    FillRound(RectF{ cx - rad, cy - rad, cx + rad, cy + rad }, rad, c, a);
}

// Anti-aliased polyline: the coverage of every segment goes into one buffer first (the brightest
// wins), so joints are not drawn twice.
void Stroke(const std::vector<PtF>& pts, float width, COLORREF c, float a = 1) {
    if (pts.size() < 2) return;
    GdiFlush();
    const float hw = width / 2;
    float minx = pts[0].x, maxx = minx, miny = pts[0].y, maxy = miny;
    for (const auto& p : pts) {
        minx = std::min(minx, p.x); maxx = std::max(maxx, p.x);
        miny = std::min(miny, p.y); maxy = std::max(maxy, p.y);
    }
    const int bx0 = std::max<int>(g_cv.clip.left, static_cast<int>(std::floor(minx - hw - 1)));
    const int by0 = std::max<int>(g_cv.clip.top, static_cast<int>(std::floor(miny - hw - 1)));
    const int bx1 = std::min<int>(g_cv.clip.right, static_cast<int>(std::ceil(maxx + hw + 1)));
    const int by1 = std::min<int>(g_cv.clip.bottom, static_cast<int>(std::ceil(maxy + hw + 1)));
    if (bx1 <= bx0 || by1 <= by0) return;
    const int bw = bx1 - bx0, bh = by1 - by0;
    std::vector<float> cov(static_cast<size_t>(bw) * bh, 0.0f);
    for (size_t i = 0; i + 1 < pts.size(); ++i) {
        const PtF p = pts[i], q = pts[i + 1];
        const int sx0 = std::max(bx0, static_cast<int>(std::floor(std::min(p.x, q.x) - hw - 1)));
        const int sx1 = std::min(bx1, static_cast<int>(std::ceil(std::max(p.x, q.x) + hw + 1)));
        const int sy0 = std::max(by0, static_cast<int>(std::floor(std::min(p.y, q.y) - hw - 1)));
        const int sy1 = std::min(by1, static_cast<int>(std::ceil(std::max(p.y, q.y) + hw + 1)));
        const float dx = q.x - p.x, dy = q.y - p.y, len2 = dx * dx + dy * dy;
        for (int y = sy0; y < sy1; ++y)
            for (int x = sx0; x < sx1; ++x) {
                const float px = x + 0.5f - p.x, py = y + 0.5f - p.y;
                const float t = len2 > 0 ? Clamp01((px * dx + py * dy) / len2) : 0;
                const float ex = px - t * dx, ey = py - t * dy;
                const float v = Clamp01(hw + 0.5f - std::sqrt(ex * ex + ey * ey));
                float& slot = cov[static_cast<size_t>(y - by0) * bw + (x - bx0)];
                if (v > slot) slot = v;
            }
    }
    for (int y = by0; y < by1; ++y)
        for (int x = bx0; x < bx1; ++x) Blend(x, y, c, cov[static_cast<size_t>(y - by0) * bw + (x - bx0)] * a);
}

void Arc(std::vector<PtF>& out, float cx, float cy, float r, float from, float to, int steps = 24) {
    for (int i = 0; i <= steps; ++i) {
        const float t = from + (to - from) * i / steps;
        out.push_back({ cx + r * std::cos(t), cy + r * std::sin(t) });
    }
}

// ---- text --------------------------------------------------------------------------------------
std::map<std::tuple<int, bool, std::wstring>, HFONT> g_fonts;

HFONT Font(float size, bool bold, const wchar_t* family) {
    const int px = std::max(6, P(size));
    const auto key = std::make_tuple(px, bold, std::wstring(family));
    auto it = g_fonts.find(key);
    if (it != g_fonts.end()) return it->second;
    // ClearType with natural widths: crisp like the rest of Windows, and even letter spacing. Text is only
    // ever drawn on opaque pixels (the rounded corners get their alpha after drawing), so subpixel text is safe.
    HFONT f = CreateFontW(-px, 0, 0, 0, bold ? FW_BOLD : FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET, OUT_TT_PRECIS,
                          CLIP_DEFAULT_PRECIS, CLEARTYPE_NATURAL_QUALITY, DEFAULT_PITCH | FF_SWISS, family);
    g_fonts[key] = f;
    return f;
}

void FreeFonts() {
    for (auto& [k, f] : g_fonts) DeleteObject(f);
    g_fonts.clear();
}

const wchar_t* IconFamily() {
    static const wchar_t* family = nullptr;
    if (family) return family;
    family = L"Segoe MDL2 Assets";
    HFONT f = CreateFontW(-16, 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                          ANTIALIASED_QUALITY, DEFAULT_PITCH, L"Segoe Fluent Icons");
    HDC dc = CreateCompatibleDC(nullptr);
    HGDIOBJ o = SelectObject(dc, f);
    wchar_t got[LF_FACESIZE] = {};
    GetTextFaceW(dc, LF_FACESIZE, got);
    SelectObject(dc, o);
    DeleteDC(dc);
    DeleteObject(f);
    if (!_wcsicmp(got, L"Segoe Fluent Icons")) family = L"Segoe Fluent Icons";
    return family;
}

std::wstring Upper(std::wstring s) {
    if (!s.empty()) CharUpperBuffW(&s[0], static_cast<DWORD>(s.size()));
    return s;
}

struct TextStyle {
    float size = 13;
    bool bold = false;
    COLORREF color = kOnSurface;
    float tracking = 0;
};

void Select(const std::wstring& text, const TextStyle& st) {
    SelectObject(g_cv.dc, Font(st.size, st.bold, Family(text)));
    SetTextCharacterExtra(g_cv.dc, st.tracking > 0 ? std::max(1, P(st.tracking)) : 0);
    SetTextColor(g_cv.dc, st.color);
}

int Width(const std::wstring& text, const TextStyle& st) {
    Select(text, st);
    SIZE sz{};
    GetTextExtentPoint32W(g_cv.dc, text.c_str(), static_cast<int>(text.size()), &sz);
    return sz.cx;
}

int LineH(const TextStyle& st, const std::wstring& sample = L"Ag") {
    Select(sample, st);
    TEXTMETRICW tm{};
    GetTextMetricsW(g_cv.dc, &tm);
    return tm.tmHeight;
}

void Text(const std::wstring& text, RECT r, const TextStyle& st,
          UINT fmt = DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS) {
    if (text.empty()) return;
    Select(text, st);
    DrawTextW(g_cv.dc, text.c_str(), static_cast<int>(text.size()), &r, fmt | DT_NOPREFIX);
}

// Word-wrapped text from r.top; returns its height. draw = false only measures.
int Wrap(const std::wstring& text, RECT r, const TextStyle& st, bool draw = true, UINT align = DT_LEFT) {
    if (text.empty()) return 0;
    Select(text, st);
    RECT m = r;
    DrawTextW(g_cv.dc, text.c_str(), static_cast<int>(text.size()), &m, DT_WORDBREAK | DT_CALCRECT | DT_NOPREFIX | align);
    if (draw) {
        r.bottom = r.top + (m.bottom - m.top);
        DrawTextW(g_cv.dc, text.c_str(), static_cast<int>(text.size()), &r, DT_WORDBREAK | DT_NOPREFIX | align);
    }
    return m.bottom - m.top;
}

// ---- icons (24 x 24 line drawings, 2 units wide) -----------------------------------------------
enum class Icon { Battery, Pin, Gear, Refresh, Chevron, Check };

void DrawIcon(Icon icon, float x, float y, float size, COLORREF c) {
    const float k = size / 24.0f;
    auto at = [&](float u, float v) { return PtF{ x + u * k, y + v * k }; };
    const float w = 2.0f * k;
    switch (icon) {
        case Icon::Battery: {
            StrokeRound(RectF{ x + 3 * k, y + 7 * k, x + 19 * k, y + 17 * k }, 2.5f * k, w, c);
            Stroke({ at(21, 10.5f), at(21, 13.5f) }, w, c);
            for (float bx : { 7.0f, 10.5f, 14.0f }) Stroke({ at(bx, 10.5f), at(bx, 13.5f) }, w, c);
            break;
        }
        case Icon::Pin:
            Stroke({ at(15, 4.5f), at(11, 8.5f), at(7, 10), at(5.5f, 11.5f), at(12.5f, 18.5f), at(14, 17), at(15.5f, 13), at(19.5f, 9) }, w, c);
            Stroke({ at(9, 15), at(4.5f, 19.5f) }, w, c);
            Stroke({ at(14.5f, 4), at(20, 9.5f) }, w, c);
            break;
        case Icon::Gear: {
            std::vector<PtF> ring;
            constexpr int kSteps = 160;
            for (int i = 0; i <= kSteps; ++i) {
                const float t = 6.2831853f * i / kSteps;
                const float wave = std::tanh(2.6f * std::cos(8 * t)) / std::tanh(2.6f);
                const float r = 7.9f + 1.25f * wave;
                ring.push_back(at(12 + r * std::cos(t), 12 + r * std::sin(t)));
            }
            Stroke(ring, w, c);
            StrokeRound(RectF{ x + 9 * k - k, y + 9 * k - k, x + 15 * k + k, y + 15 * k + k }, 4 * k, w, c);
            break;
        }
        case Icon::Refresh: {
            std::vector<PtF> a, b;
            Arc(a, x + 12 * k, y + 12.1f * k, 8.05f * k, -0.14f, -2.74f);
            Stroke(a, w, c);
            Stroke({ at(4, 5), at(4, 9), at(8, 9) }, w, c);
            Arc(b, x + 12 * k, y + 11.9f * k, 8.05f * k, 3.0f, 0.4f);
            Stroke(b, w, c);
            Stroke({ at(20, 19), at(20, 15), at(16, 15) }, w, c);
            break;
        }
        case Icon::Chevron:
            Stroke({ at(6, 9), at(12, 15), at(18, 9) }, 3 * k, c);
            break;
        case Icon::Check:
            Stroke({ at(5, 12.5f), at(10, 17.5f), at(19.5f, 7) }, 3 * k, c);
            break;
    }
}

void AddHit(const RECT& r, H kind, int index = -1) {
    RECT v = r;
    if (kind != H::Tab && kind != H::Pin && kind != H::Gear && kind != H::Refresh && kind != H::Drop) {
        // inside the scrolling body: only the visible part counts
        IntersectRect(&v, &r, &g_body);
        if (IsRectEmpty(&v)) return;
    }
    g_hits.push_back({ v, kind, index });
}

// The pill-shaped tab button: accent when selected; otherwise a field-coloured plate with an outline (like the
// drop-down fields), so it reads as clickable, and a lighter outline and faint plate under the mouse.
constexpr int kPillGap = 6;  // between neighbouring pills, so their outlines don't touch
int PillWidth(const std::wstring& label) { return Width(label, { 12 }) + P(20); }
void Pill(const RECT& r, const std::wstring& label, bool selected, bool hover) {
    const RectF f{ (float)r.left, (float)r.top, (float)r.right, (float)r.bottom };
    if (selected) {
        FillRound(f, F(6), kAccent);
    } else {
        FillRound(f, F(6), kField);
        if (hover) FillRound(f, F(6), kOnSurface, 0.08f);
        StrokeRound(f, F(6), 1, hover ? kMuted : kOutline);
    }
    Text(label, r, { 12, false, selected ? kOnAccent : kOnSurface }, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
}

void IconButton(Icon icon, int x, int y, float size, bool active, bool hover) {
    const int box = P(size + 8);
    if (hover) FillRound(RectF{ (float)x, (float)y, (float)(x + box), (float)(y + box) }, F(5), kOnSurface, 0.08f);
    const float off = (box - F(size)) / 2;
    DrawIcon(icon, x + off, y + off, F(size), (active || hover) ? kAccent : kMuted);
}

// Section heading inside a tab ("DEVICES"): small, bold, muted, spaced out.
int Header(const std::wstring& text, int x, int y, int right) {
    const TextStyle st{ 11, true, kMuted, 0.3f };
    const int h = LineH(st);
    Text(Upper(text), RECT{ x, y, right, y + h }, st);
    return h;
}

// ---- tabs --------------------------------------------------------------------------------------
std::wstring TabLabel(Tab t) {
    switch (t) {
        case Tab::Devices: return T(L"Devices");
        case Tab::History: return T(L"History");
        default: return T(L"Settings");
    }
}

const Device* FindDevice(const std::wstring& key) {
    for (const auto& d : g_devs)
        if (d.key == key) return &d;
    return nullptr;
}

COLORREF LevelColor(const Device& d) {
    if (!d.connected || d.dim) return kOutline;
    if (d.charging) return kGo;
    if (d.low) return kError;
    return kAccent;
}

// Parts joined by " · " into lines no wider than `width`: a line breaks between parts, never inside
// one ("Last reading: just now" stays whole). A part wider than a line gets its own, shortened with "...".
std::vector<std::wstring> PackParts(const std::vector<std::wstring>& parts, int width, const TextStyle& st) {
    const std::wstring sep = L"  \u00B7  ";
    std::vector<std::wstring> lines;
    std::wstring line;
    for (const auto& part : parts) {
        if (part.empty()) continue;
        if (line.empty()) line = part;
        else if (Width(line + sep + part, st) <= width) line += sep + part;
        else { lines.push_back(line); line = part; }
    }
    if (!line.empty()) lines.push_back(line);
    return lines;
}

std::vector<std::wstring> SplitParts(const std::wstring& text) {
    const std::wstring sep = L"  \u00B7  ";
    std::vector<std::wstring> out;
    size_t start = 0;
    for (;;) {
        const size_t at = text.find(sep, start);
        out.push_back(text.substr(start, at == std::wstring::npos ? std::wstring::npos : at - start));
        if (at == std::wstring::npos) break;
        start = at + sep.size();
    }
    return out;
}

int DrawDevices(int x, int y, int right) {
    const int top = y;
    y += Header(TabLabel(Tab::Devices), x, y, right) + P(4);
    if (g_devs.empty()) {
        y += Wrap(T(L"No devices with battery info found"), RECT{ x, y, right, y + P(400) }, { 12, false, kMuted }) + P(4);
    }
    const TextStyle nameSt{ 13, false, kOnSurface }, subSt{ 12, false, kMuted };
    const int nameH = LineH(nameSt), subH = LineH(subSt);
    // the shown devices first, then the hidden ones under their own heading
    std::vector<size_t> order;
    for (int pass = 0; pass < 2; ++pass)
        for (size_t i = 0; i < g_devs.size(); ++i)
            if (g_devs[i].hidden == (pass == 1)) order.push_back(i);
    bool hiddenHeader = false;
    for (size_t i : order) {
        const Device& d = g_devs[i];
        if (d.hidden && !hiddenHeader) {
            hiddenHeader = true;
            y += P(10);
            y += Header(T(L"Hidden"), x, y, right) + P(4);
        }
        // Columns: chip, icon, text (name, then connection and state, then the tracking details, wrapped),
        // a fixed slot for the "..." button, then level and time to empty against the right edge, so nothing
        // overlaps or moves when the button appears.
        const COLORREF levelC = d.dim || !d.connected ? kMuted : d.low ? kError : d.charging ? kGo : kOnSurface;
        const std::wstring tte = d.timeToEmpty.empty() ? std::wstring() : TF(L"{time} to empty", { { L"time", d.timeToEmpty } });
        const int moreSlot = P(28);
        const int levelRight = right - P(4);
        const int levelW = std::max(Width(d.level, { 13, true }), tte.empty() ? 0 : Width(tte, subSt)) + P(4);
        const int textX = x + P(4) + P(3) + P(8) + P(22) + P(8);
        const int textRight = levelRight - levelW - moreSlot - P(8);
        std::vector<std::wstring> lines = PackParts({ d.connection, d.status }, textRight - textX, subSt);
        for (auto& l : PackParts(SplitParts(d.details), textRight - textX, subSt)) lines.push_back(l);
        const int rowH = std::max(P(4) + nameH + static_cast<int>(lines.size()) * (subH + P(1)) + P(4),
                                  P(8) + nameH + (tte.empty() ? 0 : P(1) + subH));
        const RECT row{ x, y, right, y + rowH };
        AddHit(row, H::Device, static_cast<int>(i));
        const bool rowHover = Hovered(H::Device, static_cast<int>(i)) || Hovered(H::Actions, static_cast<int>(i));
        if (rowHover)
            FillRound(RectF{ (float)row.left, (float)row.top, (float)row.right, (float)row.bottom }, F(4), kOnSurface, 0.06f);
        int cx = x + P(4);
        // level chip, like the team colour bar in the standings
        const float chipH = std::min(F(30), rowH - F(10));
        const float cy = y + rowH / 2.0f;
        FillRound(RectF{ (float)cx, cy - chipH / 2, (float)cx + F(3), cy + chipH / 2 }, F(1), LevelColor(d));
        cx += P(3) + P(8);
        if (d.glyph) {
            const std::wstring g(1, d.glyph);
            SelectObject(g_cv.dc, Font(18, false, IconFamily()));
            SetTextCharacterExtra(g_cv.dc, 0);
            SetTextColor(g_cv.dc, d.dim || !d.connected ? kOutline : kMuted);
            RECT gr{ cx, y, cx + P(22), y + rowH };
            DrawTextW(g_cv.dc, g.c_str(), 1, &gr, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
        }
        const COLORREF nameC = d.dim || !d.connected ? kMuted : kOnSurface;
        int ty = y + P(4);
        Text(d.name, RECT{ textX, ty, textRight, ty + nameH }, { 13, false, nameC });
        ty += nameH + P(1);
        for (const auto& l : lines) {
            Text(l, RECT{ textX, ty, textRight, ty + subH }, subSt);
            ty += subH + P(1);
        }
        // level (and time to empty under it), centred on the row like the chip and the icon
        const int blockH = nameH + (tte.empty() ? 0 : P(1) + subH);
        const int ly = y + (rowH - blockH) / 2;
        Text(d.level, RECT{ levelRight - levelW, ly, levelRight, ly + nameH }, { 13, true, levelC },
             DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
        if (!tte.empty())
            Text(tte, RECT{ levelRight - levelW, ly + nameH + P(1), levelRight, ly + nameH + P(1) + subH }, subSt,
                 DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
        if (rowHover || (g_open == kDeviceActions && g_actionsKey == d.key)) {  // "...": the device's actions
            const int box = P(24);
            const int mr = levelRight - levelW - P(4);
            const RECT more{ mr - box, y + (rowH - box) / 2, mr, y + (rowH + box) / 2 };
            AddHit(more, H::Actions, static_cast<int>(i));
            const bool hot = Hovered(H::Actions, static_cast<int>(i));
            FillRound(RectF{ (float)more.left, (float)more.top, (float)more.right, (float)more.bottom }, F(5), kOnSurface,
                      hot ? 0.12f : 0.06f);
            for (int k = -1; k <= 1; ++k)
                Dot((more.left + more.right) / 2.0f + k * F(5), (more.top + more.bottom) / 2.0f, F(1.4f), hot ? kAccent : kMuted);
        }
        y += rowH + P(4);
    }
    if (!g_devs.empty()) {
        y += P(6);
        y += Wrap(T(L"Click a device for its history. Right-click it to show or hide it, move it, rename it or set its battery alert."),
                  RECT{ x, y, right, y + P(400) }, { 12, false, kMuted }) + P(4);
    }
    if (!g_unsupported.empty()) {
        y += P(12);
        y += Header(TF(L"No battery info ({count})", { { L"count", std::to_wstring(g_unsupported.size()) } }), x, y, right) + P(4);
        for (const auto& n : g_unsupported) {
            const int h = LineH(nameSt) + P(8);
            Text(n, RECT{ x + P(4), y, right, y + h }, { 13, false, kMuted });
            y += h + P(4);
        }
        const std::wstring link = T(L"Request support for a device...");
        const int h = LineH({ 13, true });
        const RECT lr{ x + P(4), y, x + P(4) + Width(link, { 13, true }), y + h };
        AddHit(lr, H::Support);
        Text(link, lr, { 13, true, Hovered(H::Support) ? kOnSurface : kAccent });
        y += h + P(4);
    }
    return y - top;
}

// ---- History -----------------------------------------------------------------------------------
std::wstring PeriodLabel(int i) {
    switch (i) {
        case 0: return T(L"24 hours");
        case 1: return T(L"7 days");
        case 2: return T(L"30 days");
        case 3: return T(L"1 year");
        default: return T(L"All time");
    }
}

std::wstring Remaining(double hours) { return battery_history::FormatHours(hours); }

std::wstring OneDecimal(double v) {
    wchar_t buf[32];
    swprintf(buf, 32, L"%.1f", v);
    return buf;
}

// Rows of pills; returns the bottom. With `rows` it only counts the rows.
int Pills(int x, int y, int right, const std::vector<std::wstring>& labels, int selected, H kind, int* rows = nullptr) {
    int cx = x;
    const int h = P(30);
    if (rows) *rows = labels.empty() ? 0 : 1;
    for (size_t i = 0; i < labels.size(); ++i) {
        const int w = std::min(PillWidth(labels[i]), right - x);
        if (cx > x && cx + w > right) {
            cx = x;
            y += h + P(4);
            if (rows) ++*rows;
        }
        if (rows) { cx += w + P(kPillGap); continue; }
        const RECT r{ cx, y, cx + w, y + h };
        AddHit(r, kind, static_cast<int>(i));
        Pill(r, labels[i], static_cast<int>(i) == selected, Hovered(kind, static_cast<int>(i)));
        cx += w + P(kPillGap);
    }
    return y + h;
}

void Card(const RECT& r) {
    const RectF f{ (float)r.left, (float)r.top, (float)r.right, (float)r.bottom };
    FillRound(f, F(8), kField);
    StrokeRound(f, F(8), 1, kOutline);
}

// A drop-down field: dark plate, outline (accent while open), the chosen text and a chevron.
void Field(const RECT& field, const std::wstring& text, bool open, bool hover, bool enabled) {
    const RectF f{ (float)field.left, (float)field.top, (float)field.right, (float)field.bottom };
    FillRound(f, F(6), kField);
    StrokeRound(f, F(6), 1, open ? kAccent : (hover && enabled) ? kMuted : kOutline);
    Text(text, RECT{ field.left + P(8), field.top, field.right - P(24), field.bottom },
         { 13, false, enabled ? kOnSurface : Mix(kSurface, kOnSurface, 0.45f) });
    DrawIcon(Icon::Chevron, field.right - F(17), field.top + (field.bottom - field.top - F(10)) / 2, F(10), kMuted);
}

RECT g_openField{};  // the open list's field, for the list drawn on top of everything

// ---- chart time axis: local calendar ticks -------------------------------------------------------
constexpr int64_t kEpochDelta = 11644473600LL;  // seconds between 1601 (FILETIME) and 1970 (Unix)

SYSTEMTIME LocalFromUnix(int64_t unix) {
    ULARGE_INTEGER u;
    u.QuadPart = static_cast<ULONGLONG>(unix + kEpochDelta) * 10000000ULL;
    FILETIME ft{ u.LowPart, u.HighPart };
    SYSTEMTIME utc{}, local{};
    FileTimeToSystemTime(&ft, &utc);
    if (!SystemTimeToTzSpecificLocalTime(nullptr, &utc, &local)) local = utc;
    return local;
}

int64_t UnixFromLocal(const SYSTEMTIME& local) {
    SYSTEMTIME utc{};
    if (!TzSpecificLocalTimeToSystemTime(nullptr, &local, &utc)) utc = local;
    FILETIME ft{};
    if (!SystemTimeToFileTime(&utc, &ft)) return 0;
    ULARGE_INTEGER u;
    u.LowPart = ft.dwLowDateTime;
    u.HighPart = ft.dwHighDateTime;
    return static_cast<int64_t>(u.QuadPart / 10000000ULL) - kEpochDelta;
}

// Local wall-clock time plus `minutes` (in local time, so day ticks stay on midnight across DST).
SYSTEMTIME AddMinutes(SYSTEMTIME st, int64_t minutes) {
    FILETIME ft{};
    SystemTimeToFileTime(&st, &ft);
    ULARGE_INTEGER u;
    u.LowPart = ft.dwLowDateTime;
    u.HighPart = ft.dwHighDateTime;
    u.QuadPart += static_cast<ULONGLONG>(minutes) * 60ULL * 10000000ULL;
    ft = { u.LowPart, u.HighPart };
    FileTimeToSystemTime(&ft, &st);
    return st;
}

std::wstring FormatLocal(const SYSTEMTIME& st, const wchar_t* dateFormat, bool time) {
    wchar_t buf[96] = {};
    std::wstring out;
    if (dateFormat && GetDateFormatEx(LOCALE_NAME_USER_DEFAULT, 0, &st, dateFormat, buf, 96, nullptr)) out = buf;
    if (time && GetTimeFormatEx(LOCALE_NAME_USER_DEFAULT, TIME_NOSECONDS, &st, nullptr, buf, 96)) {
        if (!out.empty()) out += L" ";
        out += buf;
    }
    return out;
}

struct Tick { int64_t at; std::wstring label; };

// Grid lines with labels on round local times: hours for a day, weekdays for a week, dates for a month,
// months for a year.
std::vector<Tick> AxisTicks(int64_t since, int64_t until) {
    std::vector<Tick> out;
    const int64_t span = until - since;
    if (span <= 0) return out;
    SYSTEMTIME st = LocalFromUnix(since);
    st.wSecond = st.wMilliseconds = 0;
    if (span > 75 * 86400LL) {  // first of each month (every 2nd/3rd for long spans)
        const int every = span > 400 * 86400LL ? 6 : span > 200 * 86400LL ? 3 : 1;
        st.wDay = 1;
        st.wHour = st.wMinute = 0;
        for (int i = 0; i < 400; ++i) {
            const int64_t at = UnixFromLocal(st);
            if (at > until) break;
            if (at >= since && (st.wMonth - 1) % every == 0)
                out.push_back({ at, FormatLocal(st, st.wMonth == 1 ? L"MMM yyyy" : L"MMM", false) });
            if (++st.wMonth > 12) { st.wMonth = 1; ++st.wYear; }
        }
        return out;
    }
    int64_t step;  // minutes
    if (span <= 3 * 3600) step = 30;
    else if (span <= 14 * 3600) step = 120;
    else if (span <= 36 * 3600) step = 360;
    else if (span <= 10 * 86400LL) step = 1440;
    else if (span <= 21 * 86400LL) step = 2 * 1440;
    else step = 7 * 1440;
    const int64_t dayStep = std::min<int64_t>(step, 1440);
    int64_t minuteOfDay = st.wHour * 60 + st.wMinute;
    minuteOfDay = minuteOfDay / dayStep * dayStep;
    st.wHour = static_cast<WORD>(minuteOfDay / 60);
    st.wMinute = static_cast<WORD>(minuteOfDay % 60);
    for (int i = 0; i < 200; ++i) {
        const int64_t at = UnixFromLocal(st);
        if (at > until) break;
        if (at >= since) {
            std::wstring label = step < 1440 ? FormatLocal(st, nullptr, true)
                               : step == 1440 ? FormatLocal(st, L"ddd", false)
                               : FormatLocal(st, L"d MMM", false);
            if (!label.empty()) CharUpperBuffW(&label[0], 1);  // some locales write weekdays in lower case
            out.push_back({ at, label });
        }
        st = AddMinutes(st, step);
    }
    return out;
}

// A dashed straight line (charging or a stretch without readings in the chart).
void Dashed(PtF a, PtF b, float width, COLORREF c, float dash, float gap, float alpha = 1) {
    const float dx = b.x - a.x, dy = b.y - a.y, len = std::sqrt(dx * dx + dy * dy);
    if (len < 0.5f) return;
    for (float t = 0; t < len; t += dash + gap) {
        const float e = std::min(len, t + dash);
        Stroke({ PtF{ a.x + dx * t / len, a.y + dy * t / len }, PtF{ a.x + dx * e / len, a.y + dy * e / len } }, width, c, alpha);
    }
}

int DrawHistory(int x, int y, int right) {
    const int top = y;
    if (g_devs.empty()) {
        y += Header(T(L"Battery history"), x, y, right) + P(4);
        y += Wrap(T(L"No devices with battery info found"), RECT{ x, y, right, y + P(400) }, { 12, false, kMuted });
        return y - top;
    }
    if (!FindDevice(g_histKey)) g_histKey = g_devs.front().key;
    const Device& dev = *FindDevice(g_histKey);

    std::vector<std::wstring> names;
    int selected = 0;
    for (size_t i = 0; i < g_devs.size(); ++i) {
        names.push_back(g_devs[i].name);
        if (g_devs[i].key == g_histKey) selected = static_cast<int>(i);
    }
    if (names.size() > 1) {
        y += Header(T(L"Device"), x, y, right) + P(6);
        // Up to three buttons (shown devices first, the chosen one always among them), the rest in a list.
        std::vector<size_t> quick;
        for (int pass = 0; pass < 2 && quick.size() < 3; ++pass)
            for (size_t i = 0; i < g_devs.size() && quick.size() < 3; ++i)
                if (g_devs[i].hidden == (pass == 1) && g_devs[i].connected) quick.push_back(i);
        for (size_t i = 0; i < g_devs.size() && quick.size() < 3; ++i)
            if (std::find(quick.begin(), quick.end(), i) == quick.end()) quick.push_back(i);
        if (std::find(quick.begin(), quick.end(), static_cast<size_t>(selected)) == quick.end()) quick.back() = static_cast<size_t>(selected);
        const bool more = g_devs.size() > quick.size();
        const std::wstring moreLabel = TF(L"More devices ({count})", { { L"count", std::to_wstring(g_devs.size() - quick.size()) } });
        const int moreW = more ? std::min(right - x, Width(moreLabel, { 13 }) + P(40)) : 0;
        int cx = x;
        const int h = P(30);
        for (size_t i : quick) {
            const int w = std::min(PillWidth(names[i]), right - x);
            if (cx > x && cx + w > right) { cx = x; y += h + P(4); }
            const RECT r{ cx, y, cx + w, y + h };
            AddHit(r, H::HistDevice, static_cast<int>(i));
            Pill(r, names[i], static_cast<int>(i) == selected, Hovered(H::HistDevice, static_cast<int>(i)));
            cx += w + P(kPillGap);
        }
        if (more) {
            if (cx > x && cx + moreW > right) { cx = x; y += h + P(4); }
            const RECT field{ cx, y + P(1), cx + moreW, y + P(29) };
            AddHit(field, H::Select, kDeviceList);
            Field(field, moreLabel, g_open == kDeviceList, Hovered(H::Select, kDeviceList), true);
            if (g_open == kDeviceList) g_openField = field;
        }
        y += h + P(12);
    }
    y += Header(T(L"Period"), x, y, right) + P(6);
    y = Pills(x, y, right, { PeriodLabel(0), PeriodLabel(1), PeriodLabel(2), PeriodLabel(3), PeriodLabel(4) }, g_period, H::Period) + P(14);

    const int64_t now = static_cast<int64_t>(std::time(nullptr));
    int64_t since = now - 7 * 86400;
    const int64_t until = now;
    switch (g_period) {
        case 0: since = now - 86400; break;
        case 2: since = now - 30 * 86400LL; break;
        case 3: since = now - 365 * 86400LL; break;
        case 4: {
            const int64_t first = battery_history::Earliest(g_histKey);
            since = first >= 0 ? std::min(first, now - 3600) : now - 86400;
            break;
        }
        default: break;
    }
    // A device with less history than the period: start the chart shortly before its first reading
    // instead of squeezing everything against the right edge.
    bool fitted = false;
    if (g_period < 4) {
        const int64_t first = battery_history::Earliest(g_histKey);
        if (first > since) {
            const int64_t start = std::max(since, first - std::max<int64_t>(1800, (now - first) / 25));
            fitted = start > since;
            since = start;
        }
    }
    const int64_t span = std::max<int64_t>(1, until - since);
    const auto points = battery_history::Readings(g_histKey, since, until);
    const battery_history::Stats stats = battery_history::Summarize(points);

    // five small cards: latest, lowest, highest, change, readings
    const int gap = P(6);
    const int cardW = (right - x - 4 * gap) / 5;
    const TextStyle labelSt{ 11, false, kMuted }, valueSt{ 16, true, kOnSurface };
    const int labelH = LineH(labelSt), valueH = LineH(valueSt);
    const int cardH = P(8) + labelH + P(2) + valueH + P(8);
    // Used: every percentage point the battery went down; Charged: how many times it went up by 5 or more
    // (also across a stretch without readings, e.g. charging while switched off).
    const std::wstring dash = L"–";
    int used = 0, charges = 0, climb = 0;
    for (size_t i = 1; i < points.size(); ++i) {
        const int d = points[i].percent - points[i - 1].percent;
        if (d < 0) {
            used -= d;
            if (climb >= 5) ++charges;
            climb = 0;
        } else {
            climb += d;
        }
    }
    if (climb >= 5) ++charges;
    const bool trend = stats.count >= 2;
    const std::wstring values[] = {
        stats.count ? std::to_wstring(stats.last) + L"%" : dash,
        stats.count ? std::to_wstring(stats.lowest) + L"%" : dash,
        stats.count ? std::to_wstring(stats.highest) + L"%" : dash,
        trend ? std::to_wstring(used) + L"%" : dash,
        trend ? (charges == 1 ? std::wstring(T(L"Once")) : TF(L"{count} times", { { L"count", std::to_wstring(charges) } })) : dash };
    const wchar_t* headings[] = { L"Latest", L"Lowest", L"Highest", L"Used", L"Charged" };
    for (int i = 0; i < 5; ++i) {
        const int cx = x + i * (cardW + gap);
        const RECT r{ cx, y, i == 4 ? right : cx + cardW, y + cardH };
        Card(r);
        Text(T(headings[i]), RECT{ r.left + P(10), r.top + P(8), r.right - P(6), r.top + P(8) + labelH }, labelSt);
        Text(values[i], RECT{ r.left + P(10), r.top + P(10) + labelH, r.right - P(6), r.top + P(10) + labelH + valueH },
             { 16, true, i == 0 ? kAccent : (i == 4 && charges > 0) ? kGo : kOnSurface });
    }
    y += cardH + P(8);

    // estimated time to empty
    const RuntimeEstimate forecast = battery_history::Forecast(g_histKey);
    std::wstring etaValue, etaDetails;
    COLORREF etaC = kAccent;
    switch (forecast.state) {
        case RuntimeState::Ready:
            etaValue = Remaining(forecast.hoursRemaining);
            etaDetails = TF(L"Recent discharge: {rate} pp/day · {count} readings",
                            { { L"rate", OneDecimal(forecast.percentPerHour * 24.0) }, { L"count", std::to_wstring(forecast.sampleCount) } });
            break;
        case RuntimeState::Charging:
            etaValue = T(L"Currently charging");
            etaDetails = T(L"No remaining-time estimate while charging.");
            etaC = kGo;
            break;
        case RuntimeState::Stale:
            etaValue = T(L"No recent readings");
            etaDetails = T(L"Connect the device to resume forecasting.");
            break;
        case RuntimeState::Flat:
            etaValue = T(L"Insufficient decline");
            etaDetails = T(L"A measurable discharge trend is required.");
            break;
        default:
            etaValue = T(L"Collecting data");
            etaDetails = T(L"Requires 4 readings, 2 hours and 3 percentage points of decline.");
            break;
    }
    const TextStyle detailSt{ 12, false, kMuted };
    const int detailH = Wrap(etaDetails, RECT{ x + P(12), 0, right - P(12), P(400) }, detailSt, false);
    const RECT eta{ x, y, right, y + P(8) + labelH + P(2) + valueH + P(4) + detailH + P(10) };
    Card(eta);
    Text(T(L"Estimated time to empty"), RECT{ x + P(12), y + P(8), right - P(12), y + P(8) + labelH }, labelSt);
    Text(etaValue, RECT{ x + P(12), y + P(10) + labelH, right - P(12), y + P(10) + labelH + valueH }, { 16, true, etaC });
    Wrap(etaDetails, RECT{ x + P(12), y + P(14) + labelH + valueH, right - P(12), eta.bottom }, detailSt);
    y = eta.bottom + P(8);

    // the chart
    const RECT chart{ x, y, right, y + P(220) };
    Card(chart);
    const std::wstring legend = TF(L"{count} readings · green: charging · dotted: no readings",
                                   { { L"count", std::to_wstring(stats.count) } });
    Text(legend, RECT{ chart.left + P(12), chart.top + P(8), chart.right - P(12), chart.top + P(8) + labelH }, labelSt);
    const RECT plot{ chart.left + P(44), chart.top + P(16) + labelH, chart.right - P(14), chart.bottom - P(28) };
    const int gw = plot.right - plot.left, gh = plot.bottom - plot.top;
    const auto xAt = [&](int64_t t) {
        return plot.left + static_cast<float>(gw) * static_cast<float>(std::clamp<int64_t>(t - since, 0, span)) / span;
    };
    for (int i = 0; i <= 4; ++i) {
        const float gy = plot.top + gh * i / 4.0f;
        FillRect(RECT{ plot.left, (LONG)std::lround(gy), plot.right, (LONG)std::lround(gy) + 1 }, kOutline, 0.7f);
        Text(std::to_wstring(100 - i * 25) + L"%", RECT{ chart.left + P(4), (LONG)gy - P(8), plot.left - P(6), (LONG)gy + P(8) },
             { 11, false, kMuted }, DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
    }
    const std::vector<Tick> ticks = AxisTicks(since, until);
    FillRect(RECT{ plot.left, plot.top, plot.left + 1, plot.bottom }, kOutline, 0.45f);
    for (const Tick& t : ticks) {
        const LONG gx = std::lround(xAt(t.at));
        FillRect(RECT{ gx, plot.top, gx + 1, plot.bottom }, kOutline, 0.45f);
    }
    if (!points.empty()) {
        const int64_t maxGap = std::max<int64_t>(4 * 3600, span / 150);
        const int64_t bridge = 3 * 86400;  // gaps up to this long get a dashed connector
        std::vector<PtF> run;
        std::vector<std::pair<PtF, PtF>> rises, charged, missing;
        auto at = [&](const battery_history::Sample& s) {
            return PtF{ std::min<float>(xAt(s.timestamp), (float)plot.right), plot.bottom - gh * s.percent / 100.0f };
        };
        SetClip(RECT{ plot.left - P(5), plot.top - P(5), plot.right + P(5), plot.bottom + P(5) });
        RECT clip = g_cv.clip;
        IntersectRect(&clip, &clip, &g_body);
        SetClip(clip);
        for (size_t i = 0; i < points.size(); ++i) {
            const PtF p = at(points[i]);
            const bool joined = i > 0 && points[i].timestamp > points[i - 1].timestamp &&
                                points[i].timestamp - points[i - 1].timestamp <= maxGap &&
                                std::abs(points[i].percent - points[i - 1].percent) <= 25;
            if (!joined) {
                Stroke(run, F(2), kAccent);
                run.clear();
                if (i > 0 && points[i].timestamp - points[i - 1].timestamp <= bridge)
                    (points[i].percent > points[i - 1].percent ? charged : missing).push_back({ at(points[i - 1]), p });
            } else if (points[i].percent > points[i - 1].percent) {
                rises.push_back({ at(points[i - 1]), p });
            }
            run.push_back(p);
        }
        for (const auto& [a, b] : missing) Dashed(a, b, F(1.5f), kMuted, F(2), F(4), 0.7f);
        for (const auto& [a, b] : charged) Dashed(a, b, F(2), kGo, F(5), F(4));
        Stroke(run, F(2), kAccent);
        for (const auto& [a, b] : rises) Stroke({ a, b }, F(2), kGo);
        const size_t step = points.size() <= 70 ? 1 : (points.size() + 59) / 60;
        for (size_t i = 0; i < points.size(); ++i) {
            const bool last = i + 1 == points.size();
            if (!last && i % step) continue;
            const PtF p = at(points[i]);
            const bool rising = i > 0 && points[i].percent > points[i - 1].percent;
            Dot(p.x, p.y, F(last ? 4.0f : 2.0f), rising ? kGo : kAccent);
        }
        SetClip(g_body);

        // Hover: thin columns across the plot; the reading nearest the mouse gets a marker and a label.
        const int colW = std::max(1, P(4));
        for (int cx = plot.left, i = 0; cx < plot.right; cx += colW, ++i)
            AddHit(RECT{ cx, plot.top, std::min<int>(cx + colW, plot.right), plot.bottom }, H::Chart, i);
        if (g_hover.kind == H::Chart) {
            const float hx = plot.left + (g_hover.index + 0.5f) * colW;
            size_t best = 0;
            float bestD = 1e9f;
            for (size_t i = 0; i < points.size(); ++i) {
                const float d = std::fabs(at(points[i]).x - hx);
                if (d < bestD) { bestD = d; best = i; }
            }
            if (bestD <= F(24)) {
                const PtF p = at(points[best]);
                FillRect(RECT{ (LONG)std::lround(p.x), plot.top, (LONG)std::lround(p.x) + 1, plot.bottom }, kMuted, 0.6f);
                Dot(p.x, p.y, F(5.5f), kField);
                Dot(p.x, p.y, F(4), kAccent);
                const std::wstring tip = std::to_wstring(points[best].percent) + L"%  ·  " +
                                         FormatLocal(LocalFromUnix(points[best].timestamp), L"ddd d MMM", true);
                const TextStyle tipSt{ 12, true, kOnSurface };
                const int tw = Width(tip, tipSt) + P(16), th = LineH(tipSt) + P(8);
                const int tx = std::clamp<int>((int)std::lround(p.x) - tw / 2, chart.left + P(4), chart.right - P(4) - tw);
                int ty = (int)std::lround(p.y) - th - P(10);
                if (ty < chart.top + P(4)) ty = (int)std::lround(p.y) + P(10);
                const RectF box{ (float)tx, (float)ty, (float)(tx + tw), (float)(ty + th) };
                FillRound(box, F(6), kField);
                StrokeRound(box, F(6), 1, kAccent);
                Text(tip, RECT{ tx, ty, tx + tw, ty + th }, tipSt, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
            }
        }
    } else {
        Text(T(L"No readings in this period"), plot, { 12, false, kMuted }, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    }
    const RECT axis{ plot.left, plot.bottom + P(6), plot.right, plot.bottom + P(6) + labelH };
    const std::wstring nowLabel = T(L"Now");
    const int nowLeft = plot.right - Width(nowLabel, labelSt);
    Text(nowLabel, axis, labelSt, DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
    int lastRight = plot.left - P(100);
    for (const Tick& t : ticks) {
        const int w = Width(t.label, labelSt);
        const int lx = std::max<int>(plot.left - P(8), (int)std::lround(xAt(t.at)) - w / 2);
        if (lx < lastRight + P(10) || lx + w > nowLeft - P(10)) continue;
        Text(t.label, RECT{ lx, axis.top, lx + w + P(2), axis.bottom }, labelSt);
        lastRight = lx + w;
    }
    y = chart.bottom + P(8);
    const std::wstring note = fitted ? TF(L"The chart starts at the first reading ({date}). Trend estimates are approximate.",
                                          { { L"date", battery_history::ShortDate(battery_history::Earliest(g_histKey)) } })
                                     : std::wstring(T(L"Trend estimates are approximate; missing samples are not inferred."));
    y += Wrap(note, RECT{ x, y, right, y + P(200) },
              { 11, false, kMuted }) + P(2);
    const std::wstring storage = battery_history::StorageSummary();
    if (!storage.empty()) y += Wrap(storage, RECT{ x, y, right, y + P(200) }, { 11, false, kMuted });
    y += P(10);

    // links: clear, custom period, larger window
    const TextStyle linkSt{ 12, true };
    const int lh = LineH(linkSt) + P(6);
    int lx = x;
    struct Link { std::wstring text; H kind; COLORREF color; };
    const Link links[] = { { T(L"Device options..."), H::Actions, kAccent }, { T(L"Custom period..."), H::Custom, kAccent },
                           { T(L"Open in a window"), H::Window, kAccent }, { T(L"Clear device history"), H::Clear, kError } };
    for (const auto& l : links) {
        const int w = Width(l.text, linkSt);
        if (lx > x && lx + w > right) { lx = x; y += lh; }
        const RECT r{ lx, y, lx + w, y + lh };
        AddHit(r, l.kind);
        Text(l.text, r, { 12, true, Hovered(l.kind) ? kOnSurface : l.color });
        lx += w + P(18);
    }
    y += lh;
    (void)dev;
    return y - top;
}

// ---- Settings ----------------------------------------------------------------------------------
std::vector<size_t> PageItems(int page, std::vector<std::wstring>* pageNames) {
    std::vector<size_t> out;
    int current = -1;
    for (size_t i = 0; i < g_sets.size(); ++i) {
        if (g_sets[i].kind == Setting::Kind::Page) {
            ++current;
            if (pageNames) pageNames->push_back(g_sets[i].label);
            continue;
        }
        if (current == page || (current < 0 && page == 0)) out.push_back(i);
    }
    return out;
}

std::wstring SelectedLabel(const Setting& s) {
    for (const auto& c : s.choices)
        if (c.selected) return c.label;
    return L"2013";
}

int DrawSettings(int x, int y, int right) {
    const int top = y;
    std::vector<std::wstring> pages;
    std::vector<size_t> items = PageItems(g_page, &pages);
    if (!pages.empty()) {
        if (g_page >= static_cast<int>(pages.size())) { g_page = 0; items = PageItems(0, nullptr); }
        y = Pills(x, y, right, pages, g_page, H::Page) + P(12);
    }
    const TextStyle labelSt{ 13, false, kOnSurface };
    const int lineH = LineH(labelSt);
    const int fieldH = P(28);
    // the label column fits the page's longest drop-down label (wrapping beyond 45% of the width)
    int labelW = P(90);
    for (size_t idx : items)
        if (g_sets[idx].kind == Setting::Kind::Select) labelW = std::max(labelW, Width(g_sets[idx].label, labelSt) + P(2));
    labelW = std::min(labelW, (right - x) * 45 / 100);
    bool first = true;
    for (size_t idx : items) {
        const Setting& s = g_sets[idx];
        const int i = static_cast<int>(idx);
        const COLORREF textC = s.enabled ? kOnSurface : Mix(kSurface, kOnSurface, 0.45f);
        switch (s.kind) {
            case Setting::Kind::Section:
                if (!first) y += P(10);
                y += Header(s.label, x, y, right) + P(6);
                break;
            case Setting::Kind::Hint:
                y += Wrap(s.label, RECT{ x + P(4), y, right - P(4), y + P(400) }, { 12, false, kMuted }) + P(8);
                break;
            case Setting::Kind::Toggle: {
                const int textX = x + P(4) + P(16) + P(8);
                const int h = std::max(P(24), Wrap(s.label, RECT{ textX, 0, right - P(4), P(400) }, labelSt, false) + P(4));
                const RECT row{ x, y, right, y + h };
                AddHit(row, H::Toggle, i);
                const bool hover = Hovered(H::Toggle, i) && s.enabled;
                const float by = y + P(2) + (lineH - F(16)) / 2;
                const RectF box{ (float)x + F(4), by, (float)x + F(20), by + F(16) };
                FillRound(box, F(3), kField);
                StrokeRound(box, F(3), 1, hover ? kMuted : kOutline);
                if (s.checked) DrawIcon(Icon::Check, box.l + F(1), box.t + F(1), F(14), s.enabled ? kOnSurface : textC);
                Wrap(s.label, RECT{ textX, y + P(2), right - P(4), y + h }, { 13, false, textC });
                y += h + P(4);
                break;
            }
            case Setting::Kind::Select: {
                const int fx = x + P(4) + labelW + P(10);
                const int labelH = Wrap(s.label, RECT{ x + P(4), 0, fx - P(8), P(400) }, labelSt, false);
                const int h = std::max(fieldH, labelH);
                Wrap(s.label, RECT{ x + P(4), y + std::max(0, (fieldH - labelH) / 2), fx - P(8), y + h }, { 13, false, textC });
                const RECT field{ fx, y, right - P(4), y + fieldH };
                AddHit(field, H::Select, i);
                Field(field, SelectedLabel(s), g_open == i, Hovered(H::Select, i), s.enabled);
                if (g_open == i) g_openField = field;
                y += h + P(6);
                break;
            }
            case Setting::Kind::Button: {
                const int w = std::min(right - x - P(4), Width(s.label, labelSt) + P(24));
                const RECT b{ x + P(4), y, x + P(4) + w, y + fieldH };
                AddHit(b, H::Button, i);
                const RectF f{ (float)b.left, (float)b.top, (float)b.right, (float)b.bottom };
                FillRound(f, F(6), !s.enabled ? kSurface : Hovered(H::Button, i) ? kButtonHover : kButton);
                StrokeRound(f, F(6), 1, kOutline);
                Text(s.label, b, { 13, false, textC }, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
                y += fieldH + P(6);
                break;
            }
            default: break;
        }
        first = false;
    }
    return y - top;
}

// The open list of a Select, drawn last so it lies on top of the rest.
std::vector<Choice> OpenChoices() {
    std::vector<Choice> out;
    if (g_open == kDeviceList) {
        for (const auto& d : g_devs) out.push_back({ d.name, 0, d.key == g_histKey });
    } else if (g_open == kDeviceActions) {
        out = g_actions;
    } else if (g_open >= 0 && g_open < static_cast<int>(g_sets.size())) {
        out = g_sets[static_cast<size_t>(g_open)].choices;
    }
    return out;
}

void DrawDropdown() {
    if (g_open < 0 || IsRectEmpty(&g_openField)) return;
    const std::vector<Choice> choices = OpenChoices();
    const struct { const std::vector<Choice>& choices; } s{ choices };
    const int itemH = P(26), pad = P(3);
    const int count = static_cast<int>(s.choices.size());
    const int below = g_body.bottom - g_openField.bottom - P(2), above = g_openField.top - g_body.top - P(2);
    const bool down = below >= std::min(count * itemH + 2 * pad, P(200)) || below >= above;
    const int room = std::max(itemH + 2 * pad, down ? below : above);
    const int visible = std::max(1, std::min(count, (room - 2 * pad) / itemH));
    g_dropScroll = std::max(0, std::min(g_dropScroll, count - visible));
    const int h = visible * itemH + 2 * pad;
    const RECT list{ g_openField.left, down ? g_openField.bottom + P(2) : g_openField.top - P(2) - h, g_openField.right,
                     down ? g_openField.bottom + P(2) + h : g_openField.top - P(2) };
    NoClip();
    const RectF f{ (float)list.left, (float)list.top, (float)list.right, (float)list.bottom };
    FillRound(f, F(6), kField);
    StrokeRound(f, F(6), 1, kOutline);
    g_hits.push_back({ list, H::None, -1 });  // the list swallows clicks between its items
    SetClip(list);
    for (int k = 0; k < visible; ++k) {
        const int i = g_dropScroll + k;
        const RECT r{ list.left + pad, list.top + pad + k * itemH, list.right - pad, list.top + pad + (k + 1) * itemH };
        const Choice& c = s.choices[static_cast<size_t>(i)];
        if (c.enabled) g_hits.push_back({ r, H::Drop, i });
        const bool hover = c.enabled && Hovered(H::Drop, i);
        if (hover) FillRound(RectF{ (float)r.left, (float)r.top, (float)r.right, (float)r.bottom }, F(4), kAccent);
        const COLORREF color = !c.enabled ? Mix(kField, kOnSurface, 0.4f) : hover ? kOnAccent : c.selected ? kAccent : kOnSurface;
        Text(c.label, RECT{ r.left + P(8), r.top, r.right - P(8), r.bottom }, { 13, c.selected && !hover, color });
    }
    if (visible < count) {  // a thin scroll mark
        const float trackH = static_cast<float>(h - 2 * pad);
        const float th = std::max(F(16), trackH * visible / count);
        const float ty = list.top + pad + (trackH - th) * g_dropScroll / std::max(1, count - visible);
        FillRound(RectF{ (float)list.right - F(5), ty, (float)list.right - F(2), ty + th }, F(1.5f), kMuted, 0.45f);
    }
    NoClip();
}

// ---- the whole panel ---------------------------------------------------------------------------
std::wstring Title() {
    switch (g_tab) {
        case Tab::Devices: return T(L"Battery levels");
        case Tab::History: {
            const Device* d = FindDevice(g_histKey);
            if (!d && !g_devs.empty()) d = &g_devs.front();
            return d ? d->name : std::wstring(T(L"Battery history"));
        }
        default: return T(L"Settings");
    }
}

void Draw(int W, int H) {
    if (!g_cv.Resize(W, H)) return;
    g_hits.clear();
    g_openField = {};
    NoClip();
    std::fill(g_cv.px, g_cv.px + static_cast<size_t>(W) * H, 0xFF000000u | (GetRValue(kSurface) << 16) |
                                                             (GetGValue(kSurface) << 8) | GetBValue(kSurface));
    const int pad = P(14);
    const int right = W - pad;
    // header: battery glyph, name, device count, pin and settings buttons
    int y = P(12);
    const int headH = P(22);
    DrawIcon(Icon::Battery, (float)pad, y + (headH - F(16)) / 2, F(16), kAccent);
    const TextStyle brand{ 11, true, kMuted, 0.6f };
    Text(L"KJ-BATTERYHUB", RECT{ pad + P(24), y, right - P(160), y + headH }, brand);
    const int box = P(22);
    const int gearX = right - box, pinX = gearX - P(8) - box;
    IconButton(Icon::Gear, gearX, y, 14, g_tab == Tab::Settings, Hovered(H::Gear));
    g_hits.push_back({ RECT{ gearX, y, gearX + box, y + box }, H::Gear, -1 });
    IconButton(Icon::Pin, pinX, y, 14, g_pinned, Hovered(H::Pin));
    g_hits.push_back({ RECT{ pinX, y, pinX + box, y + box }, H::Pin, -1 });
    size_t connected = 0;
    for (const auto& d : g_devs) connected += d.connected ? 1 : 0;
    const std::wstring count = connected == 1 ? std::wstring(T(L"1 device"))
                                              : TF(L"{count} devices", { { L"count", std::to_wstring(connected) } });
    Text(count, RECT{ pad + P(130), y, pinX - P(8), y + headH }, { 11, false, kMuted }, DT_RIGHT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    y += headH + P(10);
    FillRect(RECT{ pad, y, right, y + 1 }, kOutline);
    y += 1 + P(10);
    // title
    const TextStyle titleSt{ 20, true, kOnSurface };
    const int titleH = LineH(titleSt, Title());
    Text(Title(), RECT{ pad, y, right, y + titleH }, titleSt);
    y += titleH + P(10);
    // tabs
    int tx = pad;
    for (int i = 0; i < 3; ++i) {
        const std::wstring label = TabLabel(static_cast<Tab>(i));
        const int w = PillWidth(label);
        const RECT r{ tx, y, tx + w, y + P(30) };
        Pill(r, label, static_cast<int>(g_tab) == i, Hovered(H::Tab, i));
        g_hits.push_back({ r, H::Tab, i });
        tx += w + P(kPillGap);
    }
    y += P(30) + P(10);
    // footer
    const int footBox = P(20);
    const int footY = H - P(8) - footBox;
    Text(g_host.footer ? g_host.footer() : L"", RECT{ pad, footY, right - footBox - P(6), footY + footBox }, { 10, false, kFaint });
    IconButton(Icon::Refresh, right - footBox, footY, 12, false, Hovered(H::Refresh));
    g_hits.push_back({ RECT{ right - footBox, footY, right, footY + footBox }, H::Refresh, -1 });
    // the scrolling body
    const int t = static_cast<int>(g_tab);
    g_body = RECT{ pad, y, right, footY - P(10) };
    const int viewH = g_body.bottom - g_body.top;
    SetClip(g_body);
    const int contentRight = right - P(4) - (g_overflow[t] ? P(6) : 0);
    g_scroll[t] = std::max(0, std::min(g_scroll[t], g_contentH[t] - viewH));
    const int cy = y + P(8) - g_scroll[t];
    int h = 0;
    switch (g_tab) {
        case Tab::Devices: h = DrawDevices(pad, cy, contentRight); break;
        case Tab::History: h = DrawHistory(pad, cy, contentRight); break;
        default: h = DrawSettings(pad, cy, contentRight); break;
    }
    g_contentH[t] = h + P(8);
    const bool overflow = g_contentH[t] > viewH;
    if (overflow) {
        const float trackH = static_cast<float>(viewH);
        const float th = std::max(F(24), trackH * viewH / g_contentH[t]);
        const float ty = g_body.top + (trackH - th) * g_scroll[t] / std::max(1, g_contentH[t] - viewH);
        const RECT thumb{ right - P(6), (LONG)ty, right, (LONG)(ty + th) };
        const bool hot = Hovered(H::Thumb) || g_thumbGrab >= 0;
        FillRound(RectF{ (float)thumb.left, ty, (float)thumb.right, ty + th }, F(3), kMuted, hot ? 0.7f : 0.45f);
        g_hits.push_back({ thumb, H::Thumb, -1 });
    }
    NoClip();
    if (g_open == kDeviceActions) g_openField = g_actionsAt;
    DrawDropdown();
    // border, then the rounded corners become see-through
    const RectF all{ 0, 0, (float)W, (float)H };
    const float rad = F(12);
    StrokeRound(all, rad, 1, kOutline, 0.55f);
    GdiFlush();
    const int r = static_cast<int>(std::ceil(rad)) + 1;
    for (int py = 0; py < H; ++py)
        for (int px = 0; px < W; ++px) {
            uint32_t& d = g_cv.px[py * W + px];
            if ((px >= r && px < W - r) || (py >= r && py < H - r)) { d |= 0xFF000000u; continue; }
            const float a = RoundCov(px + 0.5f, py + 0.5f, all, rad);
            const uint32_t A = static_cast<uint32_t>(a * 255 + 0.5f);
            const uint32_t R = ((d >> 16) & 255) * A / 255, G = ((d >> 8) & 255) * A / 255, B = (d & 255) * A / 255;
            d = (A << 24) | (R << 16) | (G << 8) | B;
        }
    if (overflow != g_overflow[t]) {  // the scrollbar came or went: lay out once more for the narrower width
        g_overflow[t] = overflow;
        Draw(W, H);
    }
}

void Present() {
    if (!g_wnd || !g_cv.dc) return;
    POINT src{ 0, 0 };
    SIZE size{ g_cv.w, g_cv.h };
    BLENDFUNCTION bf{ AC_SRC_OVER, 0, 255, AC_SRC_ALPHA };
    UpdateLayeredWindow(g_wnd, nullptr, &g_pos, &size, g_cv.dc, &src, 0, &bf, ULW_ALPHA);
}

void Render() {
    if (!g_wnd || !IsWindowVisible(g_wnd)) return;
    Draw(g_w, g_h);
    Present();
}

void LoadData() {
    g_devs = g_host.devices ? g_host.devices() : std::vector<Device>();
    g_unsupported = g_host.unsupported ? g_host.unsupported() : std::vector<std::wstring>();
    g_sets = g_host.settings ? g_host.settings() : std::vector<Setting>();
    if (g_open < kDeviceList && g_open >= static_cast<int>(g_sets.size())) g_open = -1;
    if (g_open == kDeviceActions && g_host.deviceActions) g_actions = g_host.deviceActions(g_actionsKey);
}

// ---- placement ---------------------------------------------------------------------------------
UINT MonitorDpi(HMONITOR mon) {
    using Fn = HRESULT(WINAPI*)(HMONITOR, int, UINT*, UINT*);
    static Fn fn = [] {
        HMODULE lib = LoadLibraryExW(L"shcore.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
        return lib ? reinterpret_cast<Fn>(GetProcAddress(lib, "GetDpiForMonitor")) : nullptr;
    }();
    UINT x = 96, y = 96;
    if (fn && SUCCEEDED(fn(mon, 0, &x, &y))) return x;
    return GetDpiForSystem();
}

// Above the taskbar next to the click (below a top taskbar, beside a side one), inside the work area.
void Place() {
    POINT pt{};
    GetCursorPos(&pt);
    HMONITOR mon = MonitorFromPoint(pt, MONITOR_DEFAULTTONEAREST);
    MONITORINFO mi{ sizeof(mi) };
    GetMonitorInfoW(mon, &mi);
    g_s = MonitorDpi(mon) / 96.0f;
    const RECT a = mi.rcWork, full = mi.rcMonitor;
    const int gap = P(kGap);
    g_w = std::min(P(kBaseW), static_cast<int>(a.right - a.left) - 2 * gap);
    g_h = std::min(P(kBaseH), static_cast<int>(a.bottom - a.top) - 2 * gap);
    int x, y;
    if (a.top > full.top) {             // top taskbar
        x = pt.x - g_w / 2; y = a.top + gap;
    } else if (a.left > full.left) {    // left
        x = a.left + gap; y = pt.y - g_h / 2;
    } else if (a.right < full.right) {  // right
        x = a.right - g_w - gap; y = pt.y - g_h / 2;
    } else {                            // bottom (also an auto-hidden taskbar)
        x = pt.x - g_w / 2; y = a.bottom - g_h - gap;
    }
    x = std::max<int>(a.left + gap, std::min<int>(x, a.right - g_w - gap));
    y = std::max<int>(a.top + gap, std::min<int>(y, a.bottom - g_h - gap));
    g_pos = { x, y };
    SetWindowPos(g_wnd, HWND_TOPMOST, x, y, g_w, g_h, SWP_NOACTIVATE);
}

// ---- input -------------------------------------------------------------------------------------
Hit HitAt(POINT p) {
    for (auto it = g_hits.rbegin(); it != g_hits.rend(); ++it)
        if (PtInRect(&it->r, p)) return *it;
    return {};
}

void SetTab(Tab t) {
    g_tab = t;
    g_open = -1;
}

// Runs something that may open a dialog or a menu: the panel stays open meanwhile.
template <typename F>
void Guarded(F&& f) {
    g_busy = true;
    f();
    g_busy = false;
    LoadData();
    if (IsWindowVisible(g_wnd)) {
        SetForegroundWindow(g_wnd);
        Render();
    }
}

void OpenActions(const std::wstring& key, POINT client);

void Click(const Hit& h, POINT screen) {
    switch (h.kind) {
        case H::Tab: SetTab(static_cast<Tab>(h.index)); break;
        case H::Gear: SetTab(g_tab == Tab::Settings ? Tab::Devices : Tab::Settings); break;
        case H::Pin:
            g_pinned = !g_pinned;
            {
                DWORD v = g_pinned ? 1 : 0;
                RegSetKeyValueW(HKEY_CURRENT_USER, APP_KEY, L"PanelPinned", REG_DWORD, &v, sizeof(v));
            }
            break;
        case H::Refresh:
            if (g_host.refresh) g_host.refresh();
            break;
        case H::Device:
            if (h.index >= 0 && h.index < static_cast<int>(g_devs.size())) {
                g_histKey = g_devs[static_cast<size_t>(h.index)].key;
                SetTab(Tab::History);
            }
            break;
        case H::HistDevice:
            if (h.index >= 0 && h.index < static_cast<int>(g_devs.size())) g_histKey = g_devs[static_cast<size_t>(h.index)].key;
            break;
        case H::Period: g_period = h.index; break;
        case H::Page: g_page = h.index; g_open = -1; g_scroll[2] = 0; break;
        case H::Toggle: {
            const Setting& s = g_sets[static_cast<size_t>(h.index)];
            if (s.enabled && s.command && g_host.command) {
                const UINT cmd = s.command;
                Guarded([cmd] { g_host.command(cmd); });
                return;
            }
            break;
        }
        case H::Button: {
            const Setting& s = g_sets[static_cast<size_t>(h.index)];
            if (s.enabled && s.command && g_host.command) {
                const UINT cmd = s.command;
                Guarded([cmd] { g_host.command(cmd); });
                return;
            }
            break;
        }
        case H::Select: {
            if (g_open == h.index) { g_open = -1; break; }
            if (h.index != kDeviceList) {
                const Setting& s = g_sets[static_cast<size_t>(h.index)];
                if (!s.enabled || s.choices.empty()) break;
            }
            g_open = h.index;
            const std::vector<Choice> choices = OpenChoices();
            g_dropScroll = 0;
            for (size_t i = 0; i < choices.size(); ++i)
                if (choices[i].selected) g_dropScroll = static_cast<int>(i) - 3;
            break;
        }
        case H::Actions: {
            const std::wstring key = h.index >= 0 && h.index < static_cast<int>(g_devs.size()) ? g_devs[static_cast<size_t>(h.index)].key : g_histKey;
            OpenActions(key, POINT{ h.index >= 0 ? h.r.right - P(280) : h.r.left, h.r.bottom + P(2) });
            return;
        }
        case H::Drop:
            if (g_open == kDeviceActions) {
                const UINT cmd = h.index >= 0 && h.index < static_cast<int>(g_actions.size()) ? g_actions[static_cast<size_t>(h.index)].command : 0;
                g_open = -1;
                if (cmd && g_host.command) {
                    Guarded([cmd] { g_host.command(cmd); });
                    return;
                }
            } else if (g_open == kDeviceList) {
                if (h.index >= 0 && h.index < static_cast<int>(g_devs.size())) g_histKey = g_devs[static_cast<size_t>(h.index)].key;
                g_open = -1;
            } else if (g_open >= 0) {
                const Setting& s = g_sets[static_cast<size_t>(g_open)];
                const UINT cmd = h.index >= 0 && h.index < static_cast<int>(s.choices.size()) ? s.choices[static_cast<size_t>(h.index)].command : 0;
                g_open = -1;
                if (cmd && g_host.command) {
                    Guarded([cmd] { g_host.command(cmd); });
                    return;
                }
            }
            break;
        case H::Clear: {
            const std::wstring key = g_histKey;
            Guarded([key] { battery_history::ClearDevice(g_wnd, key); });
            return;
        }
        case H::Custom:
        case H::Window: {
            const std::wstring key = g_histKey;
            const bool custom = h.kind == H::Custom;
            if (g_host.historyWindow) {
                g_busy = true;
                g_host.historyWindow(key, custom);
                g_busy = false;
            }
            return;
        }
        case H::Support:
            if (g_host.command) Guarded([] { g_host.command(0); });
            return;
        default: break;
    }
    (void)screen;
    Render();
}

// The device's actions as a list under the mouse (right-click on a device, or "Device options...").
void OpenActions(const std::wstring& key, POINT client) {
    if (!g_host.deviceActions) return;
    g_actions = g_host.deviceActions(key);
    if (g_actions.empty()) return;
    g_actionsKey = key;
    const int w = P(280);
    const int x = std::max<int>(g_body.left, std::min<int>(client.x, g_body.right - w));
    g_actionsAt = RECT{ x, client.y - P(1), x + w, client.y };
    g_open = kDeviceActions;
    g_dropScroll = 0;
    Render();
}

void Scroll(int delta) {
    const int t = static_cast<int>(g_tab);
    if (g_open >= 0) {
        g_dropScroll -= delta;
    } else {
        g_scroll[t] -= delta * P(30);
    }
    Render();
}

void CycleTab(int step) {
    SetTab(static_cast<Tab>((static_cast<int>(g_tab) + 3 + step) % 3));
    Render();
}

LRESULT CALLBACK Proc(HWND w, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
        case WM_MOUSEMOVE: {
            POINT p{ (short)LOWORD(lp), (short)HIWORD(lp) };
            if (g_thumbGrab >= 0) {
                const int t = static_cast<int>(g_tab);
                const int viewH = g_body.bottom - g_body.top;
                const float th = std::max(F(24), static_cast<float>(viewH) * viewH / std::max(1, g_contentH[t]));
                const float range = viewH - th;
                if (range > 0) g_scroll[t] = static_cast<int>((p.y - g_thumbGrab - g_body.top) / range * (g_contentH[t] - viewH));
                Render();
                return 0;
            }
            Hit h = HitAt(p);
            if (!Same(h, g_hover)) {
                g_hover = h;
                Render();
            }
            TRACKMOUSEEVENT tme{ sizeof(tme), TME_LEAVE, w, 0 };
            TrackMouseEvent(&tme);
            return 0;
        }
        case WM_MOUSELEAVE:
            if (g_hover.kind != H::None) { g_hover = {}; Render(); }
            return 0;
        case WM_SETCURSOR: {
            POINT p;
            GetCursorPos(&p);
            ScreenToClient(w, &p);
            const Hit h = HitAt(p);
            const bool hand = h.kind != H::None && h.kind != H::Thumb && h.kind != H::Chart;
            SetCursor(LoadCursorW(nullptr, hand ? IDC_HAND : IDC_ARROW));
            return TRUE;
        }
        case WM_LBUTTONDOWN: {
            POINT p{ (short)LOWORD(lp), (short)HIWORD(lp) };
            g_press = HitAt(p);
            if (g_press.kind == H::Thumb) {
                g_thumbGrab = p.y - g_press.r.top;
                SetCapture(w);
            }
            return 0;
        }
        case WM_LBUTTONUP: {
            POINT p{ (short)LOWORD(lp), (short)HIWORD(lp) };
            if (g_thumbGrab >= 0) {
                g_thumbGrab = -1;
                ReleaseCapture();
                Render();
                return 0;
            }
            const Hit h = HitAt(p);
            const bool same = Same(h, g_press);
            g_press = {};
            if (!same) return 0;
            if (g_open >= 0 && h.kind != H::Drop && h.kind != H::None) {
                const bool reopen = h.kind == H::Select && h.index == g_open;
                g_open = -1;
                if (reopen) { Render(); return 0; }
            } else if (g_open >= 0 && h.kind == H::None) {
                if (!PtInRect(&h.r, p)) g_open = -1;  // a click beside the list closes it
                Render();
                return 0;
            }
            POINT screen = p;
            ClientToScreen(w, &screen);
            Click(h, screen);
            return 0;
        }
        case WM_RBUTTONUP: {
            POINT p{ (short)LOWORD(lp), (short)HIWORD(lp) };
            const Hit h = HitAt(p);
            if ((h.kind == H::Device || h.kind == H::HistDevice) && h.index >= 0 && h.index < static_cast<int>(g_devs.size()))
                OpenActions(g_devs[static_cast<size_t>(h.index)].key, p);
            else if (g_open >= 0) { g_open = -1; Render(); }
            return 0;
        }
        case WM_MOUSEWHEEL:
            Scroll(GET_WHEEL_DELTA_WPARAM(wp) / WHEEL_DELTA);
            return 0;
        case WM_KEYDOWN:
            switch (wp) {
                case VK_ESCAPE:
                    if (g_open >= 0) { g_open = -1; Render(); }
                    else Hide();
                    return 0;
                case VK_LEFT: CycleTab(-1); return 0;
                case VK_RIGHT: CycleTab(1); return 0;
                case VK_UP: Scroll(1); return 0;
                case VK_DOWN: Scroll(-1); return 0;
                case VK_PRIOR: Scroll(8); return 0;
                case VK_NEXT: Scroll(-8); return 0;
                case 'R':
                    if (g_host.refresh) g_host.refresh();
                    return 0;
                case VK_OEM_COMMA:
                    if (GetKeyState(VK_CONTROL) < 0) { SetTab(Tab::Settings); Render(); }
                    return 0;
            }
            break;
        case WM_ACTIVATE:
            if (LOWORD(wp) == WA_INACTIVE && !g_pinned && !g_busy && IsWindowVisible(w)) Hide();
            return 0;
        case WM_DPICHANGED:
            return 0;  // placed again on every opening
        case WM_DISPLAYCHANGE:
        case WM_SETTINGCHANGE:
            if (IsWindowVisible(w)) Hide();
            return 0;
        case WM_CLOSE:
            Hide();
            return 0;
    }
    return DefWindowProcW(w, msg, wp, lp);
}

bool EnsureWindow() {
    if (g_wnd) return true;
    WNDCLASSEXW wc{ sizeof(wc) };
    wc.hInstance = g_inst;
    wc.lpfnWndProc = Proc;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.lpszClassName = kClass;
    wc.hIcon = LoadIconW(g_inst, MAKEINTRESOURCEW(1));
    if (!RegisterClassExW(&wc) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) return false;
    g_wnd = CreateWindowExW(WS_EX_LAYERED | WS_EX_TOOLWINDOW | WS_EX_TOPMOST, kClass, L"KJ-BatteryHub", WS_POPUP, 0, 0,
                            P(kBaseW), P(kBaseH), nullptr, nullptr, g_inst, nullptr);
    return g_wnd != nullptr;
}

bool WriteBmp(const std::wstring& path) {
    BITMAPFILEHEADER fh{};
    BITMAPINFOHEADER ih{};
    ih.biSize = sizeof(ih);
    ih.biWidth = g_cv.w;
    ih.biHeight = -g_cv.h;
    ih.biPlanes = 1;
    ih.biBitCount = 32;
    ih.biCompression = BI_RGB;
    const DWORD bytes = static_cast<DWORD>(g_cv.w) * g_cv.h * 4;
    fh.bfType = 0x4D42;
    fh.bfOffBits = sizeof(fh) + sizeof(ih);
    fh.bfSize = fh.bfOffBits + bytes;
    HANDLE f = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (f == INVALID_HANDLE_VALUE) return false;
    DWORD n = 0;
    bool ok = WriteFile(f, &fh, sizeof(fh), &n, nullptr) && WriteFile(f, &ih, sizeof(ih), &n, nullptr) &&
              WriteFile(f, g_cv.px, bytes, &n, nullptr);
    CloseHandle(f);
    return ok;
}

}  // namespace

void Init(HINSTANCE inst, const Host& host) {
    g_inst = inst;
    g_host = host;
    DWORD v = 0, sz = sizeof(v);
    if (RegGetValueW(HKEY_CURRENT_USER, APP_KEY, L"PanelPinned", RRF_RT_REG_DWORD, nullptr, &v, &sz) == ERROR_SUCCESS)
        g_pinned = v != 0;
}

bool Visible() { return g_wnd && IsWindowVisible(g_wnd); }

void Show(Tab tab, const std::wstring& key) {
    if (!EnsureWindow()) return;
    SetTab(tab);
    if (!key.empty()) g_histKey = key;
    g_scroll[static_cast<int>(tab)] = 0;
    g_hover = {};
    LoadData();
    if (!IsWindowVisible(g_wnd)) Place();
    ShowWindow(g_wnd, SW_SHOWNOACTIVATE);
    Render();
    SetForegroundWindow(g_wnd);
    SetFocus(g_wnd);
}

void Toggle(Tab tab, const std::wstring& key) {
    if (Visible()) {
        if (tab == Tab::History && !key.empty() && (g_tab != Tab::History || key != g_histKey)) { Show(tab, key); return; }
        Hide();
        return;
    }
    // The click that closed the panel (it lost the focus to the taskbar) is this same click.
    if (g_hiddenAt && GetTickCount64() - g_hiddenAt < 350) return;
    Show(tab, key);
}

void Hide() {
    if (!g_wnd) return;
    if (IsWindowVisible(g_wnd)) g_hiddenAt = GetTickCount64();
    ShowWindow(g_wnd, SW_HIDE);
    g_open = -1;
    g_thumbGrab = -1;
    g_cv.Free();
    FreeFonts();
    memory_trim::Soon();
}

void Update() {
    if (!Visible() || g_busy) return;
    LoadData();
    Render();
}

bool SaveShots(const std::wstring& folder) {
    g_s = 1.5f;
    LoadData();
    bool ok = true;
    auto shot = [&](Tab tab, int page, const std::wstring& name) {
        SetTab(tab);
        g_page = page;
        for (int i = 0; i < 2; ++i) Draw(P(kBaseW), P(kBaseH));  // twice: the scrollbar decision settles
        ok = WriteBmp(folder + L"\\" + name + L".bmp") && ok;
    };
    shot(Tab::Devices, 0, L"panel-devices");
    if (!g_devs.empty() && g_host.deviceActions) {  // a device's actions, opened by a right-click
        g_actions = g_host.deviceActions(g_devs.front().key);
        g_actionsKey = g_devs.front().key;
        g_actionsAt = RECT{ P(140), P(200), P(140) + P(280), P(201) };
        g_open = kDeviceActions;
        for (int i = 0; i < 2; ++i) Draw(P(kBaseW), P(kBaseH));
        WriteBmp(folder + L"\\panel-device-actions.bmp");
        g_open = -1;
    }
    if (!g_devs.empty()) g_histKey = g_devs.front().key;
    shot(Tab::History, 0, L"panel-history");
    g_scroll[1] = 100000;
    shot(Tab::History, 0, L"panel-history-end");
    g_scroll[1] = 0;
    SetTab(Tab::History);
    g_open = kDeviceList;
    if (g_devs.size() > 2) g_histKey = g_devs[2].key;
    for (int i = 0; i < 2; ++i) Draw(P(kBaseW), P(kBaseH));
    WriteBmp(folder + L"\\panel-history-list.bmp");
    g_open = -1;
    std::vector<std::wstring> pages;
    PageItems(0, &pages);
    for (size_t i = 0; i < std::max<size_t>(1, pages.size()); ++i)
        shot(Tab::Settings, static_cast<int>(i), L"panel-settings-" + std::to_wstring(i + 1));
    g_cv.Free();
    FreeFonts();
    return ok;
}

}  // namespace panel
