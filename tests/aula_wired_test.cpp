// Wired AULA keyboards (258A:010B..010E) in the production AULA poller with a simulated HID transport.
// The reply layout (level at byte 134 of feature report 06) follows github.com/Makkksx/AulaMonitor;
// no capture from a real keyboard is used here.
#include "../src/drivers/drivers.h"

#include <cstdio>

void PollAula(PollContext& ctx);

namespace {
int failures = 0, setFeatures = 0, getFeatures = 0;
BYTE level = 83;
bool answer = true;
std::wstring product = L"AULA F99";
const char* current = "";

void Check(bool result, const char* expression, int line) {
    if (!result) {
        std::printf("FAIL %s:%d %s\n", current, line, expression);
        ++failures;
    }
}
#define CHECK(x) Check((x), #x, __LINE__)

HidColl Keyboard(USHORT pid) {
    HidColl c;
    c.vid = 0x258A;
    c.pid = pid;
    c.iface = 1;
    c.path = L"wired";
    c.caps.UsagePage = 0xFF00;
    c.caps.Usage = 0x0001;
    c.caps.FeatureReportByteLength = 520;
    return c;
}

PollContext Poll(std::initializer_list<HidColl> colls) {
    setFeatures = getFeatures = 0;
    PollContext ctx;
    ctx.colls = colls;
    PollAula(ctx);
    return ctx;
}
}  // namespace

bool HidDev::Open(const HidColl& c) {
    c_ = c;
    return true;
}
bool HidDev::OpenFeatureOnly(const HidColl& c) { return Open(c); }
void HidDev::Close() {}
void HidDev::Flush() {}
bool HidDev::Write(const std::vector<BYTE>&, int) { return false; }
int HidDev::Read(std::vector<BYTE>&, int) { return -1; }
bool HidDev::SetFeature(const std::vector<BYTE>& data) {
    ++setFeatures;
    std::vector<BYTE> expected(520, 0);
    expected[0] = 0x06;
    expected[1] = 0x84;
    expected[4] = 0x01;
    expected[6] = 0x80;
    CHECK(data == expected);
    return true;
}
bool HidDev::GetFeature(BYTE reportId, std::vector<BYTE>& out) {
    ++getFeatures;
    CHECK(reportId == 0x06);
    out.assign(520, 0);
    out[0] = reportId;
    out[8] = 17;  // what the VGN driver's decoder would have read
    if (answer) out[134] = level;
    return true;
}
std::wstring HidProduct(const HidColl&) { return product; }
std::wstring Hex4(USHORT v) {
    wchar_t b[8];
    swprintf(b, 8, L"%04X", v);
    return b;
}

int main() {
    current = "F99 on the cable, 010E";
    auto ctx = Poll({ Keyboard(0x010E) });
    CHECK(ctx.out.size() == 1);
    if (ctx.out.size() == 1) {
        const auto& s = ctx.out[0];
        CHECK(s.percent == 83 && s.charging && s.via == L"USB cable" && s.kind == DeviceKind::Keyboard);
        CHECK(s.key == L"aula:258A:010E" && s.name == L"AULA F99");
    }
    CHECK(ctx.IsClaimed(0x258A, 0x010E) && setFeatures == 1);

    current = "AULA on 010C: read here, kept from the VGN driver";
    level = 42;
    ctx = Poll({ Keyboard(0x010C) });
    CHECK(ctx.out.size() == 1 && ctx.out[0].percent == 42 && ctx.IsClaimed(0x258A, 0x010C));

    current = "AULA keyboard that does not answer: claimed, nothing listed";
    answer = false;
    ctx = Poll({ Keyboard(0x010C) });
    CHECK(ctx.out.empty() && ctx.IsClaimed(0x258A, 0x010C) && getFeatures == 3);
    answer = true;

    current = "VXE V87 Pro on 010C is left to the VGN driver";
    product = L"VXE V87 Pro";
    ctx = Poll({ Keyboard(0x010C) });
    CHECK(ctx.out.empty() && !ctx.IsClaimed(0x258A, 0x010C) && setFeatures == 0);

    current = "unnamed product that does not answer is not claimed";
    product = L"";
    answer = false;
    ctx = Poll({ Keyboard(0x010C) });
    CHECK(ctx.out.empty() && !ctx.IsClaimed(0x258A, 0x010C));
    answer = true;
    ctx = Poll({ Keyboard(0x010C) });
    CHECK(ctx.out.size() == 1 && ctx.out[0].name == L"AULA wired keyboard");
    product = L"AULA F99";

    current = "other collections and ids are not touched";
    auto small = Keyboard(0x010E);
    small.caps.FeatureReportByteLength = 65;
    ctx = Poll({ small, Keyboard(0x0110) });
    CHECK(ctx.out.empty() && setFeatures == 0);

    if (failures) return 1;
    std::puts("AULA wired keyboard tests passed");
    return 0;
}
