#include "../src/history_model.h"
#include <cassert>
#include <cstdio>

int main() {
    using namespace battery_history;
    const int64_t now = 1795000000;
    Sample a{L"coolermaster:demo", now-7200, 85, false};
    assert(Valid(a));
    Sample decoded;
    assert(Decode(Encode(a).substr(0, Encode(a).size()-1), decoded));
    assert(decoded.key==a.key && decoded.percent==a.percent && decoded.timestamp==a.timestamp);
    assert(!Decode("1\tevil\t20\t0", decoded));
    assert(!Decode("1795000000\t00000001\t101\t0", decoded));
    assert(!Decode("1795000000\t00000001\t15\t5", decoded));
    assert(!Decode("1795000000\t00000000\t15\t0", decoded));
    std::wstring unicode = L"Mysz:ą🎧";
    assert(UnhexKey(HexKey(unicode), decoded.key) && decoded.key == unicode);
    assert(!UnhexKey("GGGGGGGG", decoded.key));
    assert(!UnhexKey("FFFF", decoded.key));
    Store s;
    assert(s.Record(a));
    assert(!s.Record(a));
    assert(!s.Record({a.key, a.timestamp+10, 80, true}));
    assert(!s.Record({a.key, a.timestamp+100, 80, false}));
    assert(s.Record({a.key, a.timestamp+100, 80, true})); // state flip
    assert(!s.Record({a.key, a.timestamp+150, 79, false})); // one-minute minimum
    assert(s.Record({a.key, a.timestamp+200, 78, false}));
    assert(!s.Record({a.key, a.timestamp+300, 77, false}));
    assert(s.Record({L"bt:other", a.timestamp+600, 70, false}));
    assert(s.Record({a.key, a.timestamp+2200, 60, false}));
    auto stats=s.Summarize(a.key,now-10000,now);
    assert(stats.count==4 && stats.first==85 && stats.last==60 && stats.lowest==60 && stats.highest==85);
    auto small=s.Summarize(a.key,a.timestamp+201,now);
    assert(small.count==1 && small.first==60);
    assert(s.For(a.key, now-200,now).empty());
    std::string persisted=s.Serialize();
    Store loaded; loaded.Load(persisted, now);
    assert(loaded.samples.size()==s.samples.size());
    loaded.Clear(a.key);
    assert(loaded.For(a.key, now-10000,now).empty());
    assert(loaded.For(L"bt:other", now-10000,now).size()==1);
    loaded.Load(persisted+"999\tBROKEN",now);
    assert(loaded.samples.size()==s.samples.size()); // ignore truncated row
    // Lifetime history: old readings are kept, only thinned.
    Store retention;
    assert(retention.Record({L"old",now-kRetentionSeconds-1,30,false}));
    assert(retention.Record({L"fresh",now,70,false}));
    assert(retention.samples.size()==2);
    assert(retention.Earliest(L"old")==now-kRetentionSeconds-1);
    assert(retention.Earliest(L"none")==-1);

    // Older than 30 days: lowest and highest level per device per 6 hours; older than a year: per day.
    {
        std::string rows;
        const int64_t slot = (now - 40LL*86400) / kCompactBucketSeconds * kCompactBucketSeconds;
        const int levels[] = {60, 55, 80, 40, 70, 65};  // one 6-hour slot, every 30 minutes
        for (int i = 0; i < 6; ++i) rows += Encode({L"mouse", slot + i*1800, levels[i], false});
        rows += Encode({L"keyboard", slot + 600, 90, false});  // same slot, another device: kept apart
        const int64_t day = (now - 400LL*86400) / kArchiveBucketSeconds * kArchiveBucketSeconds;
        for (int h = 0; h < 24; ++h) rows += Encode({L"mouse", day + h*3600, 50 + h, false});
        for (int m = 0; m < 10; ++m) rows += Encode({L"mouse", now - 3600 + m*60, 20, false});  // recent: all kept
        Store thin; thin.Load(rows, now);
        auto old = thin.For(L"mouse", slot, slot + kCompactBucketSeconds - 1);
        assert(old.size()==2 && old[0].percent==80 && old[1].percent==40);  // highest and lowest, in time order
        assert(thin.For(L"keyboard", 0, now).size()==1);
        auto archived = thin.For(L"mouse", day, day + kArchiveBucketSeconds - 1);
        assert(archived.size()==2 && archived[0].percent==50 && archived[1].percent==73);
        assert(thin.For(L"mouse", now - 3600, now).size()==10);
        const size_t once = thin.samples.size();
        thin.Compact(now);  // running it again changes nothing
        assert(thin.samples.size()==once);
        Store again; again.Load(thin.Serialize(), now);
        assert(again.samples.size()==once);
    }

    // Keep period: readings older than it are deleted on load and on each new reading; 0 keeps everything.
    {
        std::string rows;
        rows += Encode({L"mouse", now - 400LL*86400, 50, false});
        rows += Encode({L"mouse", now - 100LL*86400, 60, false});
        rows += Encode({L"mouse", now - 40LL*86400, 70, false});
        rows += Encode({L"mouse", now - 3600, 80, false});
        Store all; all.Load(rows, now);
        assert(all.samples.size()==4 && all.Expired(now)==0);
        Store probe; probe.Load(rows, now); probe.keepSeconds = 90LL*86400;
        assert(probe.Expired(now)==2 && probe.samples.size()==4);  // counting deletes nothing
        Store ninety; ninety.keepSeconds = 90LL*86400; ninety.Load(rows, now);
        assert(ninety.samples.size()==2 && ninety.Earliest(L"mouse")==now - 40LL*86400);
        Store month; month.keepSeconds = 30LL*86400; month.Load(rows, now);
        assert(month.samples.size()==1);
        assert(month.Record({L"mouse", now + 86400*31LL, 75, false}));  // a month later: the old one goes
        assert(month.samples.size()==1 && month.samples[0].percent==75);
    }

    // Safety cap (far above real use).
    {
        std::string rows;
        for (size_t i = 0; i < kMaxSamples + 500; ++i) rows += Encode({L"d" + std::to_wstring(i % 7), now - (int64_t)i, 50, false});
        Store cap; cap.Load(rows, now);
        assert(cap.samples.size()==kMaxSamples);
    }
    puts("Battery History & Insights policy tests passed");
}
