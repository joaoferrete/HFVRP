#pragma once

#include "instance.hpp"
#include "solution.hpp"

#include <string>

namespace hfvrp {

// Metadados opcionais sobrepostos ao SVG. `exact` liga o bloco com limites,
// gap e numero de nos, que so faz sentido para o modelo exato.
struct VisualExtras {
    double       runtime_sec   = 0.0;
    bool         exact         = false;
    std::string  status;
    double       lower_bound   = 0.0;
    double       root_lp_bound = 0.0;
    double       gap           = 0.0;
    long         num_nodes     = 0;
    bool         optimal       = false;
};

// Desenha as rotas em um arquivo SVG. A cor de cada cliente indica prioridade
// e o raio indica demanda; cada veiculo recebe uma cor propria. Legivel ate
// cerca de 30 clientes; acima disso o desenho sai correto mas carregado.
void write_svg(const Instance& inst,
               const Solution& sol,
               double beta,
               const std::string& method,
               const std::string& path,
               const VisualExtras& extras = {});

} // namespace hfvrp
