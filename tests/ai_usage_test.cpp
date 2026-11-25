#include "../src/metrics/ai_usage.h"

#include <cassert>
#include <cstdio>

int main() {
    using namespace metrics;

    const std::string ok =
        R"({"usage":{"rolling":{"status":"ok","percent":30,"resetsAt":"2026-11-21T15:00:00.000Z"},)"
        R"("weekly":{"status":"rate-limited","percent":100,"resetsAt":"2026-11-23T00:00:00.000Z"},)"
        R"("monthly":{"status":"ok","percent":50,"resetsAt":"2026-12-01T00:00:00.000Z"}}})";

    AiUsage a{};
    assert(ParseAiUsage(ok, a));
    assert(a.ok);
    assert(a.error == AiError::None);
    assert(a.meter[0].percent == 30 && !a.meter[0].limited);
    assert(a.meter[1].percent == 100 && a.meter[1].limited);
    assert(a.meter[2].percent == 50 && !a.meter[2].limited);
    assert(FormatAiUsage(a) == L"30/100/50");
    assert(AiSeverity(a) == 2);

    AiUsage warn = a;
    warn.meter[1] = { 80, false, "" };
    warn.meter[2] = { 20, false, "" };
    assert(AiSeverity(warn) == 1);

    AiUsage normal = a;
    normal.meter[0] = { 10, false, "" };
    normal.meter[1] = { 20, false, "" };
    normal.meter[2] = { 30, false, "" };
    assert(AiSeverity(normal) == 0);

    AiUsage bad{};
    assert(!ParseAiUsage("{}", bad));
    assert(bad.error == AiError::Parse);
    assert(!bad.ok);
    assert(FormatAiUsage(bad) == L"--");
    assert(AiSeverity(bad) == 0);

    assert(FormatAiReset("2026-11-21T15:00:00.000Z") == L"2026-11-21 15:00 UTC");
    assert(FormatAiReset("") == L"");
    assert(FormatAiReset("basura") == L"");
    assert(FormatAiReset("2026-11-21T15:00:00+00:00") == L"");  // only a trailing-Z ISO is accepted

    const std::string withNull =
        R"({"usage":{"rolling":null,)"
        R"("weekly":{"status":"ok","percent":10,"resetsAt":"2026-11-23T00:00:00.000Z"},)"
        R"("monthly":{"status":"ok","percent":20,"resetsAt":"2026-12-01T00:00:00.000Z"}}})";
    AiUsage nulled{};
    assert(!ParseAiUsage(withNull, nulled));
    assert(nulled.error == AiError::Parse);

    const std::string overPercent =
        R"({"usage":{"rolling":{"status":"ok","percent":101,"resetsAt":""},)"
        R"("weekly":{"status":"ok","percent":10,"resetsAt":""},)"
        R"("monthly":{"status":"ok","percent":20,"resetsAt":""}}})";
    AiUsage over{};
    assert(!ParseAiUsage(overPercent, over));

    AiUsage limited{};
    limited.ok = true;
    limited.meter[1] = { 10, true, "" };  // rate-limited below every threshold still reads critical
    assert(AiSeverity(limited) == 2);

    std::puts("ai_usage_test: all passed");
    return 0;
}
