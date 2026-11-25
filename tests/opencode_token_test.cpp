#include "../src/metrics/opencode_token.h"

#include <cassert>
#include <cstdio>
#include <vector>

int main() {
    std::vector<BYTE> blob;
    std::wstring back;
    assert(ProtectToken(L"sk-test-123", blob));
    assert(!blob.empty());
    assert(UnprotectToken(blob, back));
    assert(back == L"sk-test-123");

    std::vector<BYTE> broken(blob.begin(), blob.end() - 1);
    assert(!UnprotectToken(broken, back));

    std::vector<BYTE> odd{ 0x01, 0x02, 0x03 };
    std::wstring wide;
    assert(!UnprotectToken(odd, wide));  // no múltiplo de sizeof(wchar_t)

    std::vector<BYTE> huge(70 * 1024, 0x41);
    assert(!UnprotectToken(huge, wide));  // por encima del límite de 64 KB

    std::puts("opencode_token_test: all passed");
    return 0;
}
