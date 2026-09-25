#pragma once

#include <string>
#include <vector>

namespace hfvrp {

struct Vehicle {
    double capacity;
    double fixed_cost;
    double variable_cost;
};

struct Instance {
    std::string name;
    int num_customers;                 // N; o deposito e o no 0, clientes vao de 1 a N
    int num_vehicles;                  // M

    std::vector<double> coord_x;       // tamanho N+1
    std::vector<double> coord_y;       // tamanho N+1
    std::vector<double> demand;        // tamanho N+1, demand[0] = 0
    std::vector<double> priority;      // tamanho N+1, priority[0] = 0
    std::vector<std::vector<double>> distance; // (N+1) x (N+1)

    std::vector<Vehicle> vehicles;     // tamanho M

    int num_nodes() const { return num_customers + 1; }

    // Constante big-M das restricoes MTZ: soma de todas as distancias mais um.
    double distance_bound() const;

    double total_capacity() const;
    double total_demand() const;
};

// Le uma instancia no formato descrito no README.
Instance load_instance(const std::string& path);

// Grava uma instancia no mesmo formato.
void save_instance(const Instance& inst, const std::string& path);

} // namespace hfvrp
