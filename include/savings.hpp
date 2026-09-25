#pragma once

#include "instance.hpp"
#include "solution.hpp"

namespace hfvrp {

// Heuristica das economias de Clarke e Wright, adaptada a frota heterogenea
// e a prioridade de entrega. As etapas sao: economias
// s_ij = D_0i + D_0j - D_ij em ordem decrescente; fusao de rotas cujos
// extremos sejam i e j e cuja demanda somada caiba na maior capacidade da
// frota; atribuicao best-fit de veiculos; reinsercao dos clientes que ficaram
// sem veiculo; troca de veiculos entre rotas; e 2-opt intrarrota.
//
// O beta participa da escolha de veiculo e do 2-opt, mas nao do numero de
// rotas, que decorre apenas das fusoes por capacidade.
Solution clarke_wright(const Instance& inst, double beta);

// Passe de 2-opt em cada rota, aceitando todo movimento que reduza o custo
// total. Devolve true se alguma rota mudou. Usado tambem pela Busca Tabu.
bool two_opt_pass(Solution& sol, const Instance& inst, double beta);

} // namespace hfvrp
