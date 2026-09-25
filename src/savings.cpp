#include "savings.hpp"

#include <algorithm>
#include <cstddef>
#include <limits>
#include <list>
#include <numeric>

using namespace std;

namespace hfvrp {

namespace {

double route_demand(const vector<int>& custs, const Instance& inst) {
    double q = 0.0;
    for (int c : custs) q += inst.demand[c];
    return q;
}

// Contribuicao de uma rota ao objetivo, supondo que o veiculo k a execute.
double route_cost(const vector<int>& custs, int k, const Instance& inst, double beta) {
    if (custs.empty()) return 0.0;
    const auto& veh = inst.vehicles[k];
    double t = 0.0, prio = 0.0;
    int prev = 0;
    for (int c : custs) {
        t += inst.distance[prev][c];
        prio += inst.priority[c] * t;
        prev = c;
    }
    const double dist = t + inst.distance[prev][0];
    return veh.fixed_cost + veh.variable_cost * dist + beta * prio;
}

} // namespace

bool two_opt_pass(Solution& sol, const Instance& inst, double beta) {
    bool any_improvement = false;
    for (auto& r : sol.routes) {
        if (r.customers.size() < 3 || r.vehicle_id < 0) continue;
        const int k = r.vehicle_id;
        bool improved = true;
        while (improved) {
            improved = false;
            const double base = route_cost(r.customers, k, inst, beta);
            for (size_t i = 0; i + 1 < r.customers.size(); ++i) {
                for (size_t j = i + 1; j < r.customers.size(); ++j) {
                    reverse(r.customers.begin() + i, r.customers.begin() + j + 1);
                    const double nc = route_cost(r.customers, k, inst, beta);
                    if (nc + 1e-9 < base) {
                        improved = true;
                        any_improvement = true;
                        goto next_iter; // restart with the improved route
                    }
                    reverse(r.customers.begin() + i, r.customers.begin() + j + 1);
                }
            }
            next_iter:;
        }
    }
    if (any_improvement) evaluate(sol, inst, beta);
    return any_improvement;
}

Solution clarke_wright(const Instance& inst, double beta) {
    const int N = inst.num_customers;
    const int M = inst.num_vehicles;

    // Nenhuma fusao pode exceder a maior capacidade da frota.
    double max_cap = 0.0;
    for (const auto& v : inst.vehicles) max_cap = max(max_cap, v.capacity);

    vector<list<int>> routes(N + 1);
    vector<int> route_of(N + 1, 0);
    vector<double> route_q(N + 1, 0.0);
    for (int i = 1; i <= N; ++i) {
        routes[i].push_back(i);
        route_of[i] = i;
        route_q[i] = inst.demand[i];
    }

    struct Saving { int i, j; double s; };
    vector<Saving> svs;
    svs.reserve(N * (N - 1) / 2);
    for (int i = 1; i <= N; ++i)
        for (int j = i + 1; j <= N; ++j)
            svs.push_back({i, j, inst.distance[0][i] + inst.distance[0][j] - inst.distance[i][j]});
    sort(svs.begin(), svs.end(), [](const Saving& a, const Saving& b) { return a.s > b.s; });

    for (const auto& sv : svs) {
        const int ri = route_of[sv.i], rj = route_of[sv.j];
        if (ri == rj) continue;
        auto& li = routes[ri];
        auto& lj = routes[rj];

        const bool i_front = (li.front() == sv.i);
        const bool i_back  = (li.back()  == sv.i);
        const bool j_front = (lj.front() == sv.j);
        const bool j_back  = (lj.back()  == sv.j);
        if (!(i_front || i_back) || !(j_front || j_back)) continue;
        if (route_q[ri] + route_q[rj] > max_cap + 1e-9) continue;

        // Orienta as rotas: li passa a terminar em i e lj a comecar em j.
        if (i_front) li.reverse();
        if (j_back)  lj.reverse();

        li.splice(li.end(), lj);
        route_q[ri] += route_q[rj];
        route_q[rj] = 0.0;
        for (int c : li) route_of[c] = ri;
    }

    // Reune as rotas nao vazias produzidas pelas fusoes.
    vector<vector<int>> collected;
    for (int r = 1; r <= N; ++r)
        if (!routes[r].empty())
            collected.emplace_back(routes[r].begin(), routes[r].end());

    // Atribuicao de veiculos. A fusao acima limita cada rota a maior
    // capacidade da frota, regra classica do Clarke-Wright; com frota
    // heterogenea, porem, apenas um veiculo tem essa capacidade. Cada rota
    // recebe entao o menor veiculo que ainda a comporte, de modo que os
    // grandes permanecam livres para as rotas que deles precisam. O que
    // sobrar vai para o reparo adiante.
    sort(collected.begin(), collected.end(),
              [&](const vector<int>& a, const vector<int>& b) {
                  return route_demand(a, inst) > route_demand(b, inst);
              });

    // Veiculos livres em ordem crescente de capacidade: best-fit vira uma
    // varredura para a frente e back() e o maior ainda disponivel.
    vector<int> avail(M);
    iota(avail.begin(), avail.end(), 0);
    sort(avail.begin(), avail.end(), [&](int a, int b) {
        return inst.vehicles[a].capacity < inst.vehicles[b].capacity;
    });

    Solution sol;
    sol.routes.assign(M, {});
    for (int k = 0; k < M; ++k) sol.routes[k].vehicle_id = -1;

    auto take_best_fit = [&](double dem) -> int {
        for (size_t i = 0; i < avail.size(); ++i)
            if (inst.vehicles[avail[i]].capacity + 1e-9 >= dem) {
                const int k = avail[i];
                avail.erase(avail.begin() + static_cast<ptrdiff_t>(i));
                return k;
            }
        return -1;
    };

    vector<int> leftover;
    for (const auto& custs : collected) {
        const int k = take_best_fit(route_demand(custs, inst));
        if (k < 0) {
            // Frota esgotada, ou nenhum veiculo restante comporta a rota.
            leftover.insert(leftover.end(), custs.begin(), custs.end());
            continue;
        }
        sol.routes[k].vehicle_id = k;
        sol.routes[k].customers  = custs;
    }

    // Reparo: os clientes sem veiculo vao para a capacidade residual das
    // rotas ja atribuidas, na posicao que menos acrescenta distancia.
    if (!leftover.empty()) {
        vector<double> load(M, 0.0);
        for (int k = 0; k < M; ++k)
            load[k] = route_demand(sol.routes[k].customers, inst);

        for (int c : leftover) {
            const double d = inst.demand[c];
            int best_k = -1;
            size_t best_pos = 0;
            double best_delta = numeric_limits<double>::infinity();

            for (int k = 0; k < M; ++k) {
                if (sol.routes[k].vehicle_id < 0) continue;
                if (load[k] + d > inst.vehicles[k].capacity + 1e-9) continue;
                const auto& seq = sol.routes[k].customers;
                for (size_t pos = 0; pos <= seq.size(); ++pos) {
                    const int prev = (pos == 0) ? 0 : seq[pos - 1];
                    const int next = (pos == seq.size()) ? 0 : seq[pos];
                    const double delta = inst.vehicles[k].variable_cost
                        * (inst.distance[prev][c] + inst.distance[c][next]
                           - inst.distance[prev][next]);
                    if (delta < best_delta) {
                        best_delta = delta; best_k = k; best_pos = pos;
                    }
                }
            }
            // O cliente pendente tambem pode abrir rota num veiculo ocioso
            // que o comporte sozinho.
            if (best_k < 0) {
                const int k = take_best_fit(d);
                if (k < 0) continue;   // genuinely no room: evaluate() flags it
                sol.routes[k].vehicle_id = k;
                sol.routes[k].customers  = {c};
                load[k] = d;
                continue;
            }
            auto& seq = sol.routes[best_k].customers;
            seq.insert(seq.begin() + static_cast<ptrdiff_t>(best_pos), c);
            load[best_k] += d;
        }
    }

    // Troca de veiculos entre rotas. O best-fit acima busca viabilidade e
    // ignora custo. Trocar os veiculos de duas rotas nao altera distancia
    // alguma, apenas os custos fixo e variavel, e so e aceita quando as duas
    // rotas continuam cabendo.
    {
        bool improved = true;
        while (improved) {
            improved = false;
            for (int a = 0; a < M && !improved; ++a) {
                if (sol.routes[a].vehicle_id < 0) continue;
                for (int b = a + 1; b < M; ++b) {
                    if (sol.routes[b].vehicle_id < 0) continue;
                    const double qa = route_demand(sol.routes[a].customers, inst);
                    const double qb = route_demand(sol.routes[b].customers, inst);
                    // Viavel apenas se cada rota couber no veiculo da outra.
                    if (inst.vehicles[b].capacity + 1e-9 < qa) continue;
                    if (inst.vehicles[a].capacity + 1e-9 < qb) continue;
                    const double before = route_cost(sol.routes[a].customers, a, inst, beta)
                                        + route_cost(sol.routes[b].customers, b, inst, beta);
                    const double after  = route_cost(sol.routes[a].customers, b, inst, beta)
                                        + route_cost(sol.routes[b].customers, a, inst, beta);
                    if (after < before - 1e-9) {
                        swap(sol.routes[a].customers, sol.routes[b].customers);
                        improved = true;
                        break;
                    }
                }
            }
        }
    }

    // Refinamento final: 2-opt intrarrota, sensivel a prioridade.
    two_opt_pass(sol, inst, beta);

    evaluate(sol, inst, beta);
    return sol;
}

} // namespace hfvrp
