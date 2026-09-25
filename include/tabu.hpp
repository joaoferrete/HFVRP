#pragma once

#include "instance.hpp"
#include "solution.hpp"
#include "utils.hpp"

namespace hfvrp {

struct TabuParams {
    int    max_iterations        = 2000;
    int    max_iter_no_improve   = 200;
    int    tabu_tenure           = 0;      // 0 usa 7 + floor(sqrt(N))
    double time_limit_sec        = 30.0;   // 0 desativa
};

// Busca tabu partindo da solucao de Clarke-Wright. A vizinhanca combina 2-opt
// intrarrota, relocate e swap entre rotas, e troca do veiculo que atende uma
// rota. A capacidade e tratada por penalidade suave, de modo que a busca pode
// atravessar solucoes inviaveis, mas o incumbente registrado e sempre viavel.
//
// A implementacao e deterministica: recebe a semente por uniformidade de
// interface e nao a utiliza.
Solution tabu_search(const Instance& inst, double beta,
                     const TabuParams& params, std::uint64_t seed);

} // namespace hfvrp
