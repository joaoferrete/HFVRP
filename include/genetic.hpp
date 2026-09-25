#pragma once

#include "instance.hpp"
#include "solution.hpp"
#include "utils.hpp"

namespace hfvrp {

struct GAParams {
    int    population_size    = 80;
    int    max_generations    = 300;
    double mutation_rate      = 0.10;   // por filho, nao por gene
    int    tournament_size    = 3;
    double elitism_fraction   = 0.10;
    double time_limit_sec     = 30.0;   // 0 desativa
};

// Algoritmo genetico sobre permutacoes dos clientes. Um decodificador parte a
// permutacao em rotas por capacidade e atribui os veiculos; metade da
// populacao inicial vem de perturbacoes da solucao de Clarke-Wright.
//
// O beta so intervem na avaliacao da solucao ja montada, nunca na particao.
Solution genetic_algorithm(const Instance& inst, double beta,
                           const GAParams& params, std::uint64_t seed);

} // namespace hfvrp
