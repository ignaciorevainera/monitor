#include "ai_usage.h"

#include <algorithm>
#include <cctype>
#include <cstring>

namespace metrics {

namespace {

bool IsWs(char c) {
    return c == ' ' || c == '\t' || c == '\r' || c == '\n';
}

bool FindKeyColon(const std::string& s, const char* key, size_t& valPos) {
    const size_t keyLen = std::strlen(key);
    bool inString = false;
    bool escape = false;
    for (size_t i = 0; i < s.size(); i++) {
        const char c = s[i];
        if (inString) {
            if (escape) {
                escape = false;
            } else if (c == '\\') {
                escape = true;
            } else if (c == '"') {
                inString = false;
            }
            continue;
        }
        if (c == '"') {
            if (i + keyLen <= s.size() && s.compare(i, keyLen, key) == 0) {
                size_t p = i + keyLen;
                while (p < s.size() && IsWs(s[p])) p++;
                if (p < s.size() && s[p] == ':') {
                    p++;
                    while (p < s.size() && IsWs(s[p])) p++;
                    if (p < s.size()) {
                        valPos = p;
                        return true;
                    }
                }
            }
            inString = true;
        }
    }
    return false;
}

bool ExtractObject(const std::string& json, const char* key, std::string& obj) {
    size_t pos = 0;
    if (!FindKeyColon(json, key, pos)) return false;
    if (json[pos] != '{') return false;

    int depth = 0;
    bool inString = false;
    bool escape = false;
    for (size_t i = pos; i < json.size(); i++) {
        const char c = json[i];
        if (inString) {
            if (escape) {
                escape = false;
            } else if (c == '\\') {
                escape = true;
            } else if (c == '"') {
                inString = false;
            }
            continue;
        }
        if (c == '"') {
            inString = true;
        } else if (c == '{') {
            depth++;
        } else if (c == '}') {
            depth--;
            if (depth == 0) {
                obj = json.substr(pos, i - pos + 1);
                return true;
            }
        }
    }
    return false;
}

bool ExtractInt(const std::string& obj, const char* key, int& out) {
    size_t pos = 0;
    if (!FindKeyColon(obj, key, pos)) return false;
    if (!std::isdigit(static_cast<unsigned char>(obj[pos]))) return false;

    int val = 0;
    size_t i = pos;
    while (i < obj.size() && std::isdigit(static_cast<unsigned char>(obj[i]))) {
        val = val * 10 + (obj[i] - '0');
        if (val > 100) return false;
        i++;
    }
    if (i < obj.size() && !IsWs(obj[i]) && obj[i] != ',' && obj[i] != '}') return false;
    out = val;
    return true;
}

bool ExtractString(const std::string& obj, const char* key, std::string& out) {
    size_t pos = 0;
    if (!FindKeyColon(obj, key, pos)) return false;
    if (obj[pos] != '"') return false;

    std::string res;
    bool escape = false;
    for (size_t i = pos + 1; i < obj.size(); i++) {
        const char c = obj[i];
        if (escape) {
            res.push_back(c);
            escape = false;
        } else if (c == '\\') {
            escape = true;
        } else if (c == '"') {
            out = res;
            return true;
        } else {
            res.push_back(c);
        }
    }
    return false;
}

bool ParseMeter(const std::string& json, const char* key, AiMeter& out) {
    std::string obj;
    if (!ExtractObject(json, key, obj)) return false;
    int pct = -1;
    std::string status;
    if (!ExtractInt(obj, "\"percent\"", pct)) return false;
    if (!ExtractString(obj, "\"status\"", status)) return false;
    std::string resetsAt;
    ExtractString(obj, "\"resetsAt\"", resetsAt);
    out.percent = pct;
    out.limited = (status == "rate-limited");
    out.resetsAt = resetsAt;
    return true;
}

}  // namespace

bool ParseAiUsage(const std::string& json, AiUsage& out) {
    AiUsage parsed{};
    if (!ParseMeter(json, "\"rolling\"", parsed.meter[0]) ||
        !ParseMeter(json, "\"weekly\"", parsed.meter[1]) ||
        !ParseMeter(json, "\"monthly\"", parsed.meter[2])) {
        out = AiUsage{};
        out.error = AiError::Parse;
        return false;
    }
    parsed.ok = true;
    parsed.error = AiError::None;
    out = parsed;
    return true;
}

std::wstring FormatAiUsage(const AiUsage& ai) {
    return FormatAiUsage(ai, AiDisplay::All);
}

std::wstring FormatAiUsage(const AiUsage& ai, AiDisplay display) {
    if (!ai.ok) return L"--";
    if (display == AiDisplay::MonthlyOnly) return std::to_wstring(ai.meter[2].percent) + L"%";
    return std::to_wstring(ai.meter[0].percent) + L"/" +
           std::to_wstring(ai.meter[1].percent) + L"/" +
           std::to_wstring(ai.meter[2].percent);
}

int AiPercent(const AiUsage& ai, AiDisplay display) {
    if (!ai.ok) return -1;
    if (display == AiDisplay::MonthlyOnly) return ai.meter[2].percent;
    return std::max({ ai.meter[0].percent, ai.meter[1].percent, ai.meter[2].percent });
}

std::wstring FormatAiReset(const std::string& iso) {
    if (iso.size() < 16 || iso[10] != 'T' || iso.back() != 'Z') return L"";
    const int digitPos[] = { 0, 1, 2, 3, 5, 6, 8, 9, 11, 12, 14, 15 };
    for (int p : digitPos) {
        if (!std::isdigit(static_cast<unsigned char>(iso[p]))) return L"";
    }
    if (iso[4] != '-' || iso[7] != '-' || iso[13] != ':') return L"";
    std::wstring out;
    out.reserve(20);
    for (int i = 0; i < 10; i++) out.push_back(static_cast<wchar_t>(iso[i]));
    out.push_back(L' ');
    for (int i = 11; i < 16; i++) out.push_back(static_cast<wchar_t>(iso[i]));
    out += L" UTC";
    return out;
}

int AiSeverity(const AiUsage& ai) {
    if (!ai.ok) return 0;
    int sev = 0;
    for (int i = 0; i < 3; i++) {
        if (ai.meter[i].limited || ai.meter[i].percent >= 100) return 2;
        if (ai.meter[i].percent >= 80) sev = 1;
    }
    return sev;
}

}  // namespace metrics
