#include "solution.hpp"

#include <iomanip>
#include <iostream>
#include <sstream>
#include <set>

namespace hfvrp {

void evaluate(Solution& sol, const Instance& inst, double beta) {
    sol.cost_operational = 0.0;
    sol.cost_priority    = 0.0;
    sol.feasible = true;

    std::set<int> served;

    for (auto& r : sol.routes) {
        if (r.customers.empty() || r.vehicle_id < 0) continue;
        const auto& veh = inst.vehicles[r.vehicle_id];

        double load = 0.0;
        double arrival = 0.0;
        int prev = 0;
        for (int c : r.customers) {
            arrival += inst.distance[prev][c];
            sol.cost_priority += inst.priority[c] * arrival;
            load += inst.demand[c];
            if (served.count(c)) sol.feasible = false;
            served.insert(c);
            prev = c;
        }
        double route_dist = arrival + inst.distance[prev][0];
        sol.cost_operational += veh.fixed_cost + veh.variable_cost * route_dist;
        if (load > veh.capacity + 1e-9) sol.feasible = false;
    }

    for (int i = 1; i <= inst.num_customers; ++i)
        if (!served.count(i)) sol.feasible = false;

    sol.cost_total = sol.cost_operational + beta * sol.cost_priority;
}

std::string validate(const Solution& sol, const Instance& inst) {
    std::set<int> served;
    for (const auto& r : sol.routes) {
        if (r.customers.empty()) continue;
        if (r.vehicle_id < 0 || r.vehicle_id >= inst.num_vehicles)
            return "route has invalid vehicle_id";
        double load = 0.0;
        for (int c : r.customers) {
            if (c < 1 || c > inst.num_customers) return "customer index out of range";
            if (served.count(c)) return "customer served more than once: " + std::to_string(c);
            served.insert(c);
            load += inst.demand[c];
        }
        if (load > inst.vehicles[r.vehicle_id].capacity + 1e-9)
            return "capacity exceeded on vehicle " + std::to_string(r.vehicle_id);
    }
    for (int i = 1; i <= inst.num_customers; ++i)
        if (!served.count(i)) return "customer not served: " + std::to_string(i);
    return "";
}

void print_solution(const Solution& sol, const Instance& inst, double beta) {
    std::cout << std::fixed << std::setprecision(3);
    std::cout << "Solution for " << inst.name
              << " (N=" << inst.num_customers
              << ", M=" << inst.num_vehicles
              << ", beta=" << beta << ")\n";

    int rid = 0;
    for (const auto& r : sol.routes) {
        if (r.customers.empty()) continue;
        ++rid;
        const auto& veh = inst.vehicles[r.vehicle_id];
        double load = 0.0, arrival = 0.0;
        int prev = 0;
        std::ostringstream seq;
        seq << "0";
        for (int c : r.customers) {
            arrival += inst.distance[prev][c];
            load += inst.demand[c];
            seq << " -> " << c << "(t=" << arrival << ",p=" << inst.priority[c] << ")";
            prev = c;
        }
        arrival += inst.distance[prev][0];
        seq << " -> 0";

        std::cout << "  Route " << rid
                  << " [vehicle " << (r.vehicle_id + 1)
                  << ", Q=" << veh.capacity
                  << ", load=" << load
                  << ", dist=" << arrival << "]: "
                  << seq.str() << '\n';
    }
    std::cout << "  cost_operational = " << sol.cost_operational << '\n';
    std::cout << "  cost_priority    = " << sol.cost_priority << '\n';
    std::cout << "  cost_total       = " << sol.cost_total << '\n';
    std::cout << "  feasible         = " << (sol.feasible ? "yes" : "no") << '\n';
}

} // namespace hfvrp
