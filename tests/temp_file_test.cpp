#include "../src/metrics/temp_file.h"

#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstring>

int main() {
    TempFileData valid{};
    valid.magic = kTempFileMagic;
    valid.tick = 10000;
    valid.tempC = 52.5;

    uint8_t buf[sizeof(TempFileData)]{};
    std::memcpy(buf, &valid, sizeof(valid));

    TempFileData parsed{};
    assert(ParseTempFile(buf, sizeof(buf), parsed));
    assert(parsed.magic == kTempFileMagic);
    assert(parsed.tick == 10000);
    assert(parsed.tempC == 52.5);

    TempFileData unchanged{};
    unchanged.magic = 123;
    unchanged.tick = 456;
    unchanged.tempC = 78.9;
    assert(!ParseTempFile(buf, sizeof(buf) - 1, unchanged));
    assert(!ParseTempFile(nullptr, sizeof(buf), unchanged));
    assert(unchanged.magic == 123);

    uint8_t badMagicBuf[sizeof(TempFileData)]{};
    TempFileData badMagic = valid;
    badMagic.magic = 0xDEADBEEF;
    std::memcpy(badMagicBuf, &badMagic, sizeof(badMagic));
    assert(!ParseTempFile(badMagicBuf, sizeof(badMagicBuf), unchanged));

    assert(FreshTemp(valid, 19999));
    assert(!FreshTemp(valid, 20000));
    assert(!FreshTemp(valid, 25000));
    assert(!FreshTemp(valid, 9999));

    TempFileData zeroTick = valid;
    zeroTick.tick = 0;
    assert(!FreshTemp(zeroTick, 5000));

    TempFileData custom = valid;
    assert(FreshTemp(custom, 10499, 500));
    assert(!FreshTemp(custom, 10500, 500));

    std::puts("temp_file_test: all passed");
    return 0;
}
