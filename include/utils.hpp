#pragma once

#include <chrono>
#include <cstdint>
#include <random>
#include <unistd.h>

namespace hfvrp {

class Timer {
public:
    Timer() : start_(std::chrono::steady_clock::now()) {}
    void reset() { start_ = std::chrono::steady_clock::now(); }
    double seconds() const {
        return std::chrono::duration<double>(
                   std::chrono::steady_clock::now() - start_)
            .count();
    }
private:
    std::chrono::steady_clock::time_point start_;
};

using Rng = std::mt19937_64;

inline Rng make_rng(std::uint64_t seed) { return Rng(seed); }

// Semente baseada em relogio, usada quando a linha de comando pede execucao
// nao reproduzivel (--seed null). Combina o tempo em nanossegundos com o PID
// para que execucoes consecutivas nao coincidam.
inline std::uint64_t auto_seed() {
    const auto now = std::chrono::high_resolution_clock::now()
                         .time_since_epoch().count();
    return static_cast<std::uint64_t>(now) ^
           (static_cast<std::uint64_t>(::getpid()) << 32);
}

} // namespace hfvrp
