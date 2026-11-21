#pragma once

namespace pawnio {

enum class PioState {
    Ok,
    NoDriver,
    NoAdmin,
    NoModule,
    Failed,
    Unsupported,
};

PioState Init();
bool ReadTemp(double& c);

}  // namespace pawnio
