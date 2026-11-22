#pragma once
// Portable, bounded battery history policy and serialization.
// Each record represents an observed level, not an estimated runtime.
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <limits>
#include <map>
#include <string>
#include <tuple>
#include <vector>

namespace battery_history {

// History is kept for the chosen period (Settings > Devices > "Keep battery history": 30 days, 90 days
// (the default), 1 year or all time); older readings are deleted. The last 30 days keep every reading (and
// are what the time-to-empty forecast uses); older readings are thinned to the lowest and highest level per
// 6 hours, and readings older than a year to the lowest and highest level per day. That keeps a year of
// history at roughly 3,000 readings per device, so memory and the file stay small.
constexpr int64_t kRetentionSeconds = 30LL * 24 * 3600;  // full detail and the forecast window
constexpr int kDefaultKeepDays = 90;                     // 0 = all time
constexpr int64_t kYearSeconds = 365LL * 24 * 3600;
constexpr int64_t kCompactBucketSeconds = 6LL * 3600;    // older than 30 days
constexpr int64_t kArchiveBucketSeconds = 24LL * 3600;   // older than a year
constexpr int64_t kCompactEverySeconds = 3600;           // thinning runs at most once an hour
constexpr int64_t kRegularIntervalSeconds = 30LL * 60;
constexpr int64_t kStateChangeIntervalSeconds = 60;
constexpr size_t kMaxSamples = 100000;  // safety cap only; thinning keeps real use far below it
constexpr size_t kMaxKeyChars = 256;

struct Sample {
    std::wstring key;
    int64_t timestamp = 0; // UTC Unix seconds
    int percent = -1;
    bool charging = false;
};

inline bool Valid(const Sample& s) {
    return !s.key.empty() && s.key.size() <= kMaxKeyChars &&
           s.key.find(L'\0') == std::wstring::npos &&
           s.timestamp >= 946684800LL && s.timestamp <= 4102444800LL &&
           s.percent >= 0 && s.percent <= 100;
}

inline std::string HexKey(const std::wstring& key) {
    static const char digits[] = "0123456789ABCDEF";
    std::string out;
    out.reserve(key.size() * 8);
    for (wchar_t wc : key) {
        uint32_t c = static_cast<uint32_t>(wc);
        for (int shift = 28; shift >= 0; shift -= 4)
            out.push_back(digits[(c >> shift) & 15]);
    }
    return out;
}

inline bool UnhexKey(const std::string& encoded, std::wstring& out) {
    if (encoded.empty() || encoded.size() % 8 || encoded.size() > kMaxKeyChars * 8) return false;
    std::wstring decoded;
    for (size_t i = 0; i < encoded.size(); i += 8) {
        uint32_t n = 0;
        for (size_t j = i; j < i + 8; ++j) {
            char c = encoded[j];
            int v = c >= '0' && c <= '9' ? c - '0'
                  : c >= 'A' && c <= 'F' ? c - 'A' + 10
                  : c >= 'a' && c <= 'f' ? c - 'a' + 10 : -1;
            if (v < 0) return false;
            n = (n << 4) | static_cast<uint32_t>(v);
        }
        if (n == 0 || n > static_cast<uint32_t>(std::numeric_limits<wchar_t>::max())) return false;
        decoded.push_back(static_cast<wchar_t>(n));
    }
    out = std::move(decoded);
    return true;
}

inline bool ParseUInt(const std::string& text, int64_t& value) {
    if (text.empty() || text.size() > 14) return false;
    int64_t n = 0;
    for (char c : text) {
        if (c < '0' || c > '9' || n > (INT64_MAX - 9) / 10) return false;
        n = n * 10 + (c - '0');
    }
    value = n;
    return true;
}

// ASCII tab-delimited format; device identity is encoded as 8-digit wchar_t hex,
// so arbitrary driver keys cannot inject rows or escape the file format.
inline std::string Encode(const Sample& s) {
    if (!Valid(s)) return {};
    return std::to_string(s.timestamp) + "\t" + HexKey(s.key) + "\t" +
           std::to_string(s.percent) + "\t" + (s.charging ? "1\n" : "0\n");
}

inline bool Decode(const std::string& row, Sample& out) {
    size_t a = row.find('\t');
    size_t b = a == std::string::npos ? a : row.find('\t', a + 1);
    size_t c = b == std::string::npos ? b : row.find('\t', b + 1);
    if (a == std::string::npos || b == std::string::npos || c == std::string::npos ||
        row.find('\t', c + 1) != std::string::npos) return false;
    Sample s;
    int64_t timestamp = 0, pct = 0, charge = 0;
    if (!ParseUInt(row.substr(0, a), timestamp) ||
        !UnhexKey(row.substr(a + 1, b - a - 1), s.key) ||
        !ParseUInt(row.substr(b + 1, c - b - 1), pct) ||
        !ParseUInt(row.substr(c + 1), charge) || charge > 1 || pct > 100) return false;
    s.timestamp = timestamp;
    s.percent = static_cast<int>(pct);
    s.charging = charge != 0;
    if (!Valid(s)) return false;
    out = std::move(s);
    return true;
}

struct Stats {
    size_t count = 0;
    int first = -1;
    int last = -1;
    int lowest = -1;
    int highest = -1;
    int64_t firstTime = 0;
    int64_t lastTime = 0;
};

// Count, first/last and lowest/highest level of points sorted by time.
inline Stats Summarize(const std::vector<Sample>& points) {
    Stats st;
    st.count = points.size();
    if (points.empty()) return st;
    st.first = points.front().percent;
    st.last = points.back().percent;
    st.firstTime = points.front().timestamp;
    st.lastTime = points.back().timestamp;
    st.lowest = st.highest = points.front().percent;
    for (const auto& p : points) {
        st.lowest = std::min(st.lowest, p.percent);
        st.highest = std::max(st.highest, p.percent);
    }
    return st;
}

class Store {
public:
    std::vector<Sample> samples;
    int64_t lastCompact = 0;  // when Compact() last ran (0 = never)
    int64_t keepSeconds = 0;  // readings older than this are deleted; 0 = keep all (the app sets the period)

    // Older than 30 days: keep only the lowest and the highest reading of each 6-hour slot (each day when
    // older than a year), per device. Running it again changes nothing.
    void Compact(int64_t now) {
        const int64_t detail = now - kRetentionSeconds, archive = now - kYearSeconds;
        struct Pick { size_t low, high; };
        std::map<std::tuple<std::wstring, int64_t, int64_t>, Pick> slots;
        for (size_t i = 0; i < samples.size(); ++i) {
            const Sample& s = samples[i];
            if (s.timestamp >= detail) continue;
            const int64_t size = s.timestamp < archive ? kArchiveBucketSeconds : kCompactBucketSeconds;
            auto [it, added] = slots.try_emplace({s.key, size, s.timestamp / size}, Pick{i, i});
            if (added) continue;
            if (s.percent < samples[it->second.low].percent) it->second.low = i;
            if (s.percent > samples[it->second.high].percent) it->second.high = i;
        }
        if (slots.empty()) return;
        std::vector<bool> keep(samples.size(), false);
        for (size_t i = 0; i < samples.size(); ++i) keep[i] = samples[i].timestamp >= detail;
        for (const auto& [slot, pick] : slots) keep[pick.low] = keep[pick.high] = true;
        size_t out = 0;
        for (size_t i = 0; i < samples.size(); ++i)
            if (keep[i]) { if (out != i) samples[out] = std::move(samples[i]); ++out; }
        samples.resize(out);
    }

    // Readings that the keep period would delete now.
    size_t Expired(int64_t now) const {
        if (keepSeconds <= 0) return 0;
        return static_cast<size_t>(std::count_if(samples.begin(), samples.end(), [&](const Sample& s) {
            return s.timestamp < now - keepSeconds;
        }));
    }

    void Prune(int64_t now) {
        const int64_t oldest = keepSeconds > 0 ? now - keepSeconds : INT64_MIN;
        samples.erase(std::remove_if(samples.begin(), samples.end(), [now, oldest](const Sample& s) {
            return !Valid(s) || s.timestamp > now + 3600 || s.timestamp < oldest;
        }), samples.end());
        if (lastCompact == 0 || now - lastCompact >= kCompactEverySeconds || now < lastCompact) {
            Compact(now);
            lastCompact = now;
        }
        if (samples.size() > kMaxSamples)
            samples.erase(samples.begin(), samples.begin() + (samples.size() - kMaxSamples));
    }

    // The time of the device's oldest reading, or -1 when there is none.
    int64_t Earliest(const std::wstring& key) const {
        int64_t first = -1;
        for (const auto& s : samples)
            if (s.key == key && (first < 0 || s.timestamp < first)) first = s.timestamp;
        return first;
    }

    bool Record(const Sample& s) {
        if (!Valid(s)) return false;
        for (auto it = samples.rbegin(); it != samples.rend(); ++it) {
            if (it->key != s.key) continue;
            const int64_t elapsed = s.timestamp - it->timestamp;
            if (elapsed < kStateChangeIntervalSeconds) return false;
            if (elapsed < kRegularIntervalSeconds && s.charging == it->charging) return false;
            break;
        }
        samples.push_back(s);
        Prune(s.timestamp);
        return true;
    }

    void Load(const std::string& content, int64_t now) {
        samples.clear();
        lastCompact = 0;
        size_t start = 0;
        while (start < content.size()) {
            const size_t end = content.find('\n', start);
            if (end == std::string::npos) break; // ignore incomplete final write
            std::string row = content.substr(start, end - start);
            if (!row.empty() && row.back() == '\r') row.pop_back();
            Sample s;
            if (Decode(row, s)) samples.push_back(std::move(s));
            if (samples.size() > kMaxSamples)
                samples.erase(samples.begin(), samples.begin() + (samples.size() - kMaxSamples));
            start = end + 1;
        }
        Prune(now);
    }

    std::string Serialize() const {
        std::string out;
        for (const auto& s : samples) out += Encode(s);
        return out;
    }

    std::vector<Sample> For(const std::wstring& key, int64_t since, int64_t now) const {
        std::vector<Sample> out;
        for (const auto& s : samples)
            if (s.key == key && s.timestamp >= since && s.timestamp <= now)
                out.push_back(s);
        std::stable_sort(out.begin(), out.end(), [](const Sample& a, const Sample& b) {
            return a.timestamp < b.timestamp;
        });
        return out;
    }

    Stats Summarize(const std::wstring& key, int64_t since, int64_t now) const;

    void Clear(const std::wstring& key) {
        samples.erase(std::remove_if(samples.begin(), samples.end(), [&](const Sample& s) {
            return s.key == key;
        }), samples.end());
    }
};
inline Stats Store::Summarize(const std::wstring& key, int64_t since, int64_t now) const {
    return battery_history::Summarize(For(key, since, now));
}
} // namespace battery_history
