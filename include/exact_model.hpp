#pragma once

#include "instance.hpp"
#include "solution.hpp"

#include <string>

namespace hfvrp {

struct ExactResult {
    Solution solution;
    double   objective      = 0.0;   // melhor incumbente; 0 quando nenhuma solucao foi achada
    double   lower_bound    = 0.0;   // melhor limite dual conhecido
    double   root_lp_bound  = 0.0;   // valor da relaxacao linear no no raiz
    double   gap            = 0.0;   // gap relativo de MIP; 1.0 quando nao ha incumbente
    double   runtime_sec    = 0.0;
    long     num_nodes      = 0;     // nos de branch-and-bound explorados
    long     num_iterations = 0;     // iteracoes de simplex
    int      num_solutions  = 0;     // solucoes inteiras viaveis encontradas
    bool     optimal        = false;
    bool     feasible_found = false;
    // "optimal", "time_limit", "node_limit", "infeasible", "unbounded",
    // "abandoned", "no_solution" ou "unknown".
    std::string status      = "unknown";
};

// Monta e resolve o modelo de programacao inteira mista.
//
// beta          peso da prioridade; 0 recupera o HFVRP sem prioridade.
// time_limit    em segundos; <= 0 desativa.
// node_limit    teto de nos de branch-and-bound; <= 0 desativa.
// threads       <= 0 mantem o padrao do resolvedor.
// mem_limit_mb  teto de memoria; respeitado pelo CPLEX e ignorado pelo CBC,
//               que so aceita o limite de nos como aproximacao.
ExactResult solve_exact(const Instance& inst, double beta,
                        double time_limit_sec, long node_limit = 0,
                        int threads = 0, double mem_limit_mb = 0.0);

} // namespace hfvrp
