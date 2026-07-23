// Host regression coverage for the LDT-less FS content-swap default.
#include "fs_content_swap_policy.h"

#include <array>
#include <cstdio>

namespace
{

struct PolicyCase
{
    const char* overrideValue;
    bool expectedEnabled;
    const char* description;
};

} // namespace

int main()
{
    constexpr std::array cases = {
        PolicyCase{ nullptr, true, "unset defaults to safe content-swap" },
        PolicyCase{ "", true, "empty override keeps the safe default" },
        PolicyCase{ "0", false, "zero selects legacy mode" },
        PolicyCase{ "1", true, "one enables content-swap" },
        PolicyCase{ "00", true, "only an exact zero selects legacy mode" },
    };

    for(const PolicyCase& policyCase : cases)
    {
        const bool actualEnabled =
            EmuFsContentSwapEnabledByOverride(policyCase.overrideValue);
        if(actualEnabled != policyCase.expectedEnabled)
        {
            std::fprintf(stderr, "FS content-swap policy failed: %s\n",
                         policyCase.description);
            return 1;
        }
    }

    std::puts("FS content-swap policy: safe default and legacy opt-out verified");
    return 0;
}
