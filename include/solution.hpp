#pragma once

#include "instance.hpp"

#include <string>
#include <vector>

namespace hfvrp {

// Rota de um unico veiculo. O deposito e implicito no inicio e no fim;
// `customers` guarda apenas a sequencia interna.
struct Route {
    int vehicle_id = -1;               // indice em Instance::vehicles, ou -1 se ocioso
    std::vector<int> customers;        // clientes na ordem de visita
};

// Uma rota por posicao de veiculo; veiculos ociosos tem `customers` vazio.
struct Solution {
    std::vector<Route> routes;

    double cost_operational = 0.0;     // custos fixos e variaveis dos veiculos usados
    double cost_priority    = 0.0;     // soma de P_i vezes a distancia acumulada ate i
    double cost_total       = 0.0;     // operacional + beta * prioridade
    bool feasible           = false;
};

// Preenche os tres campos de custo e a viabilidade.
void evaluate(Solution& sol, const Instance& inst, double beta);

// Verifica cobertura, capacidade e indices de veiculo. Devolve string vazia
// quando a solucao e valida, ou a descricao da primeira violacao encontrada.
std::string validate(const Solution& sol, const Instance& inst);

void print_solution(const Solution& sol, const Instance& inst, double beta);

} // namespace hfvrp
