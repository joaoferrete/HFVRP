#include "tabu.hpp"
#include "savings.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

using namespace std;

namespace hfvrp {

namespace {

// Coeficiente que multiplica o excesso de carga. A penalidade entra direto no
// objetivo da busca, permitindo atravessar regioes inviaveis quando a solucao
// inicial de Clarke-Wright ja viola capacidade.
constexpr double CAPACITY_PENALTY = 1.0e4;

double route_demand(const vector<int>& custs, const Instance& inst) {
    double q = 0.0;
    for (int c : custs) q += inst.demand[c];
    return q;
}

bool capacity_ok(const Solution& sol, const Instance& inst) {
    for (const auto& r : sol.routes) {
        if (r.customers.empty() || r.vehicle_id < 0) continue;
        if (route_demand(r.customers, inst) > inst.vehicles[r.vehicle_id].capacity + 1e-9)
            return false;
    }
    return true;
}

// Custo que guia a busca: custo real mais penalidade por unidade de carga
// acima da capacidade. O coeficiente e alto o bastante para que toda solucao
// viavel supere qualquer inviavel de porte comparavel.
double penalized_cost(const Solution& sol, const Instance& inst, double beta) {
    double op_cost = 0.0, prio_cost = 0.0, overflow = 0.0;
    for (const auto& r : sol.routes) {
        if (r.customers.empty() || r.vehicle_id < 0) continue;
        const auto& veh = inst.vehicles[r.vehicle_id];
        double t = 0.0, load = 0.0;
        int prev = 0;
        for (int c : r.customers) {
            t += inst.distance[prev][c];
            prio_cost += inst.priority[c] * t;
            load += inst.demand[c];
            prev = c;
        }
        op_cost += veh.fixed_cost + veh.variable_cost * (t + inst.distance[prev][0]);
        if (load > veh.capacity) overflow += load - veh.capacity;
    }
    return op_cost + beta * prio_cost + CAPACITY_PENALTY * overflow;
}

double full_cost(const Solution& sol, const Instance& inst, double beta) {
    // Custo real, sem penalidade. Usado apenas ao registrar o incumbente.
    double op_cost = 0.0, prio_cost = 0.0;
    for (const auto& r : sol.routes) {
        if (r.customers.empty() || r.vehicle_id < 0) continue;
        const auto& veh = inst.vehicles[r.vehicle_id];
        double t = 0.0;
        int prev = 0;
        for (int c : r.customers) {
            t += inst.distance[prev][c];
            prio_cost += inst.priority[c] * t;
            prev = c;
        }
        op_cost += veh.fixed_cost + veh.variable_cost * (t + inst.distance[prev][0]);
    }
    return op_cost + beta * prio_cost;
}

struct TabuList {
    vector<int> last_iter;   // per customer, last iteration it was moved
    int tenure;

    TabuList(int N, int tenure_) : last_iter(N + 1, -1000000), tenure(tenure_) {}
    bool is_tabu(int c, int iter) const { return iter - last_iter[c] < tenure; }
    void mark(int c, int iter) { last_iter[c] = iter; }
};

} // namespace

Solution tabu_search(const Instance& inst, double beta,
                     const TabuParams& params, uint64_t seed) {
    (void)seed;  // a busca e deterministica; a semente vem por uniformidade de interface

    Solution current = clarke_wright(inst, beta);
    Solution best    = current;
    const bool start_feasible = capacity_ok(best, inst);
    double best_cost = start_feasible ? full_cost(best, inst, beta)
                                      : numeric_limits<double>::infinity();

    const int N = inst.num_customers;
    int tenure = params.tabu_tenure;
    if (tenure <= 0) tenure = 7 + (int)floor(sqrt((double)N));

    TabuList tabu(N, tenure);
    Timer timer;
    int no_improve = 0;

    for (int iter = 0; iter < params.max_iterations; ++iter) {
        if (params.time_limit_sec > 0 && timer.seconds() > params.time_limit_sec) break;
        if (no_improve >= params.max_iter_no_improve) break;

        struct Candidate {
            double new_cost = numeric_limits<double>::infinity();
            Solution sol;
            int moved_a = -1, moved_b = -1;
            bool is_tabu = false;
        };
        Candidate best_cand, best_aspir;

        auto consider = [&](Solution&& cand, int a, int b) {
            const double c = penalized_cost(cand, inst, beta);
            const bool tbu = (a > 0 && tabu.is_tabu(a, iter)) ||
                             (b > 0 && tabu.is_tabu(b, iter));
            if (!tbu) {
                if (c < best_cand.new_cost) {
                    best_cand = {c, move(cand), a, b, false};
                }
            } else if (c < best_cost) {
                if (c < best_aspir.new_cost) {
                    best_aspir = {c, move(cand), a, b, true};
                }
            }
        };

        // Relocate: move um cliente de r1 para a melhor posicao de r2.
        for (int r1 = 0; r1 < (int)current.routes.size(); ++r1) {
            const auto& src = current.routes[r1];
            if (src.customers.empty() || src.vehicle_id < 0) continue;
            for (size_t i = 0; i < src.customers.size(); ++i) {
                const int c = src.customers[i];
                for (int r2 = 0; r2 < (int)current.routes.size(); ++r2) {
                    if (r2 == r1) continue;
                    const auto& dst = current.routes[r2];
                    const size_t max_pos = dst.customers.size() + 1;
                    for (size_t p = 0; p < max_pos; ++p) {
                        Solution cand = current;
                        cand.routes[r1].customers.erase(cand.routes[r1].customers.begin() + i);
                        if (cand.routes[r2].vehicle_id < 0) cand.routes[r2].vehicle_id = r2;
                        cand.routes[r2].customers.insert(cand.routes[r2].customers.begin() + p, c);
                        consider(move(cand), c, -1);
                    }
                }
            }
        }

        // Swap: troca um cliente entre duas rotas.
        for (int r1 = 0; r1 < (int)current.routes.size(); ++r1) {
            const auto& a = current.routes[r1];
            if (a.customers.empty() || a.vehicle_id < 0) continue;
            for (int r2 = r1 + 1; r2 < (int)current.routes.size(); ++r2) {
                const auto& b = current.routes[r2];
                if (b.customers.empty() || b.vehicle_id < 0) continue;
                for (size_t i = 0; i < a.customers.size(); ++i)
                    for (size_t j = 0; j < b.customers.size(); ++j) {
                        Solution cand = current;
                        swap(cand.routes[r1].customers[i], cand.routes[r2].customers[j]);
                        consider(move(cand), a.customers[i], b.customers[j]);
                    }
            }
        }

        // 2-opt dentro de uma mesma rota.
        for (int r = 0; r < (int)current.routes.size(); ++r) {
            const auto& route = current.routes[r];
            const int sz = (int)route.customers.size();
            if (sz < 3 || route.vehicle_id < 0) continue;
            for (int i = 0; i + 1 < sz; ++i)
                for (int j = i + 1; j < sz; ++j) {
                    Solution cand = current;
                    reverse(cand.routes[r].customers.begin() + i,
                                 cand.routes[r].customers.begin() + j + 1);
                    consider(move(cand), route.customers[i], route.customers[j]);
                }
        }

        // Troca de veiculo: passa uma rota para um veiculo ocioso.
        {
            vector<bool> used(inst.num_vehicles, false);
            for (const auto& r : current.routes)
                if (!r.customers.empty() && r.vehicle_id >= 0) used[r.vehicle_id] = true;

            for (int r = 0; r < (int)current.routes.size(); ++r) {
                const auto& rt = current.routes[r];
                if (rt.customers.empty() || rt.vehicle_id < 0) continue;
                const double dem = route_demand(rt.customers, inst);
                for (int k = 0; k < inst.num_vehicles; ++k) {
                    if (used[k] && k != rt.vehicle_id) continue;
                    if (k == rt.vehicle_id) continue;
                    if (inst.vehicles[k].capacity + 1e-9 < dem) continue;
                    Solution cand = current;
                    cand.routes[r].vehicle_id = k;
                    // A rota muda de posicao para a do veiculo k, mantendo a
                    // correspondencia entre indice e veiculo.
                    if (k != r && cand.routes[k].customers.empty()) {
                        cand.routes[k] = cand.routes[r];
                        cand.routes[r] = {};
                    }
                    consider(move(cand), -1, -1);
                }
            }
        }

        // Escolhe o movimento.
        Candidate* chosen = nullptr;
        if (best_cand.new_cost < numeric_limits<double>::infinity()) chosen = &best_cand;
        if (best_aspir.new_cost < best_cand.new_cost) chosen = &best_aspir;
        if (!chosen) break;

        current = move(chosen->sol);
        if (chosen->moved_a > 0) tabu.mark(chosen->moved_a, iter);
        if (chosen->moved_b > 0) tabu.mark(chosen->moved_b, iter);

        // O incumbente viavel e registrado a parte da trajetoria da busca,
        // que pode passar por solucoes inviaveis.
        if (capacity_ok(current, inst)) {
            const double real = full_cost(current, inst, beta);
            if (real + 1e-9 < best_cost) {
                best_cost = real;
                best = current;
                no_improve = 0;
            } else {
                ++no_improve;
            }
        } else {
            ++no_improve;
        }
    }

    evaluate(best, inst, beta);
    return best;
}

} // namespace hfvrp
