#include "genetic.hpp"
#include "savings.hpp"

#include <algorithm>
#include <cstddef>
#include <limits>
#include <numeric>
#include <unordered_set>

using namespace std;

namespace hfvrp {

namespace {

using Perm = vector<int>;

// Converte uma permutacao em solucao: corta a sequencia em no maximo M
// trechos contiguos e entrega a cada trecho um veiculo que o comporte.
//
// Como a frota e heterogenea, o teto do corte e a maior capacidade ainda nao
// atribuida, e nao a maior capacidade da frota. Cada trecho fechado recebe o
// menor veiculo que ainda o comporte, o que mantem os grandes livres para os
// trechos que precisam deles.
Solution decode(const Perm& perm, const Instance& inst, double beta) {
    const int M = inst.num_vehicles;

    // Veiculos ainda livres, em ordem crescente de capacidade: assim o
    // best-fit e uma varredura para a frente e back() e o teto corrente.
    vector<int> avail(M);
    iota(avail.begin(), avail.end(), 0);
    sort(avail.begin(), avail.end(), [&](int a, int b) {
        return inst.vehicles[a].capacity < inst.vehicles[b].capacity;
    });

    Solution sol;
    sol.routes.assign(M, {});
    for (int k = 0; k < M; ++k) sol.routes[k].vehicle_id = -1;

    auto ceiling_of = [&]() -> double {
        return avail.empty() ? 0.0 : inst.vehicles[avail.back()].capacity;
    };
    // Menor veiculo livre que comporte `dem`, retirado do conjunto.
    auto take_best_fit = [&](double dem) -> int {
        for (size_t i = 0; i < avail.size(); ++i)
            if (inst.vehicles[avail[i]].capacity + 1e-9 >= dem) {
                const int k = avail[i];
                avail.erase(avail.begin() + static_cast<ptrdiff_t>(i));
                return k;
            }
        return -1;
    };

    vector<int> current, leftover;
    double cur_demand = 0.0;
    double ceiling = ceiling_of();

    auto close_chunk = [&]() {
        if (current.empty()) return;
        const int k = take_best_fit(cur_demand);
        if (k < 0) {
            // Nao deveria ocorrer enquanto cur_demand <= ceiling; guarda os
            // clientes em vez de descarta-los caso o arredondamento falhe.
            leftover.insert(leftover.end(), current.begin(), current.end());
        } else {
            sol.routes[k].vehicle_id = k;
            sol.routes[k].customers  = current;
        }
        current.clear();
        cur_demand = 0.0;
        ceiling = ceiling_of();
    };

    for (int c : perm) {
        const double d = inst.demand[c];
        if (!current.empty() && cur_demand + d > ceiling + 1e-9) close_chunk();
        // Frota esgotada, ou cliente mais pesado que todo veiculo restante.
        if (avail.empty() || d > ceiling + 1e-9) { leftover.push_back(c); continue; }
        current.push_back(c);
        cur_demand += d;
    }
    close_chunk();

    // Reparo: o corte guloso pode esgotar a frota enquanto ainda ha folga
    // espalhada pelas rotas montadas. Os clientes pendentes vao para essa
    // folga, na posicao que menos acrescenta distancia.
    if (!leftover.empty()) {
        vector<double> load(M, 0.0);
        for (int k = 0; k < M; ++k)
            for (int c : sol.routes[k].customers) load[k] += inst.demand[c];

        for (int c : leftover) {
            const double d = inst.demand[c];
            int best_k = -1;
            size_t best_pos = 0;
            double best_delta = numeric_limits<double>::infinity();

            for (int k = 0; k < M; ++k) {
                if (sol.routes[k].vehicle_id < 0) continue;
                if (load[k] + d > inst.vehicles[k].capacity + 1e-9) continue;
                const auto& seq = sol.routes[k].customers;
                for (size_t p = 0; p <= seq.size(); ++p) {
                    const int prev = (p == 0) ? 0 : seq[p - 1];
                    const int next = (p == seq.size()) ? 0 : seq[p];
                    const double delta = inst.vehicles[k].variable_cost
                        * (inst.distance[prev][c] + inst.distance[c][next]
                           - inst.distance[prev][next]);
                    if (delta < best_delta) {
                        best_delta = delta; best_k = k; best_pos = p;
                    }
                }
            }
            // Sem folga em lugar algum: o cliente fica sem atendimento e
            // evaluate() marca a solucao como inviavel.
            if (best_k < 0) continue;
            auto& seq = sol.routes[best_k].customers;
            seq.insert(seq.begin() + static_cast<ptrdiff_t>(best_pos), c);
            load[best_k] += d;
        }
    }

    evaluate(sol, inst, beta);
    return sol;
}

// Solucoes inviaveis recebem uma penalidade alta: permanecem na populacao,
// contribuindo com diversidade, mas sem pressao de selecao a seu favor.
double fitness_of(const Solution& s) {
    return s.feasible ? s.cost_total : s.cost_total + 1.0e7;
}

Perm order_crossover(const Perm& a, const Perm& b, Rng& rng) {
    const int n = (int)a.size();
    uniform_int_distribution<int> pick(0, n - 1);
    int i = pick(rng), j = pick(rng);
    if (i > j) swap(i, j);
    Perm child(n, -1);
    unordered_set<int> taken;
    for (int k = i; k <= j; ++k) { child[k] = a[k]; taken.insert(a[k]); }
    int bpos = (j + 1) % n;
    int pos  = (j + 1) % n;
    while (pos != i) {
        while (taken.count(b[bpos])) bpos = (bpos + 1) % n;
        child[pos] = b[bpos];
        bpos = (bpos + 1) % n;
        pos  = (pos + 1) % n;
    }
    return child;
}

void swap_mutate(Perm& p, Rng& rng) {
    const int n = (int)p.size();
    uniform_int_distribution<int> pick(0, n - 1);
    int a = pick(rng), b = pick(rng);
    swap(p[a], p[b]);
}

int tournament(const vector<double>& fit, int size, Rng& rng) {
    uniform_int_distribution<int> pick(0, (int)fit.size() - 1);
    int best = pick(rng);
    for (int i = 1; i < size; ++i) {
        const int c = pick(rng);
        if (fit[c] < fit[best]) best = c;
    }
    return best;
}

} // namespace

Solution genetic_algorithm(const Instance& inst, double beta,
                           const GAParams& params, uint64_t seed) {
    const int N = inst.num_customers;
    Rng rng = make_rng(seed);

    // Permutacao extraida de Clarke-Wright. Mesmo quando a solucao e
    // inviavel, a ordem dos clientes e um ponto de partida razoavel.
    Solution cw = clarke_wright(inst, beta);
    Perm cw_perm;
    cw_perm.reserve(N);
    for (const auto& r : cw.routes)
        for (int c : r.customers) cw_perm.push_back(c);
    unordered_set<int> present(cw_perm.begin(), cw_perm.end());
    for (int i = 1; i <= N; ++i) if (!present.count(i)) cw_perm.push_back(i);

    const int pop_sz = params.population_size;
    vector<Perm> pop;
    pop.reserve(pop_sz);

    // Metade da populacao perturba Clarke-Wright, metade e sorteada.
    for (int i = 0; i < pop_sz / 2; ++i) {
        Perm p = cw_perm;
        const int swaps = max(1, N / 8);
        for (int s = 0; s < swaps; ++s) swap_mutate(p, rng);
        pop.push_back(move(p));
    }
    for (int i = (int)pop.size(); i < pop_sz; ++i) {
        Perm p(N);
        iota(p.begin(), p.end(), 1);
        shuffle(p.begin(), p.end(), rng);
        pop.push_back(move(p));
    }

    vector<Solution> sol(pop_sz);
    vector<double> fit(pop_sz);
    for (int i = 0; i < pop_sz; ++i) {
        sol[i] = decode(pop[i], inst, beta);
        fit[i] = fitness_of(sol[i]);
    }

    int best_idx = 0;
    for (int i = 1; i < pop_sz; ++i) if (fit[i] < fit[best_idx]) best_idx = i;
    Solution best_sol = sol[best_idx];
    double best_fit = fit[best_idx];

    Timer timer;
    const int n_elite = max(1, (int)(params.elitism_fraction * pop_sz));

    for (int gen = 0; gen < params.max_generations; ++gen) {
        if (params.time_limit_sec > 0 && timer.seconds() > params.time_limit_sec) break;

        vector<int> order(pop_sz);
        iota(order.begin(), order.end(), 0);
        sort(order.begin(), order.end(),
                  [&](int a, int b) { return fit[a] < fit[b]; });

        vector<Perm> new_pop;
        new_pop.reserve(pop_sz);
        for (int i = 0; i < n_elite; ++i) new_pop.push_back(pop[order[i]]);

        uniform_real_distribution<double> uni(0.0, 1.0);
        while ((int)new_pop.size() < pop_sz) {
            const int p1 = tournament(fit, params.tournament_size, rng);
            const int p2 = tournament(fit, params.tournament_size, rng);
            Perm child = order_crossover(pop[p1], pop[p2], rng);
            if (uni(rng) < params.mutation_rate) swap_mutate(child, rng);
            new_pop.push_back(move(child));
        }

        pop = move(new_pop);
        for (int i = 0; i < pop_sz; ++i) {
            sol[i] = decode(pop[i], inst, beta);
            fit[i] = fitness_of(sol[i]);
            if (fit[i] < best_fit) { best_fit = fit[i]; best_sol = sol[i]; }
        }
    }

    evaluate(best_sol, inst, beta);
    return best_sol;
}

} // namespace hfvrp
