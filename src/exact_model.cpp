#include "exact_model.hpp"
#include "utils.hpp"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <vector>

#include <CoinPackedMatrix.hpp>
#include <CoinPackedVector.hpp>
#include <OsiClpSolverInterface.hpp>
#include <CbcModel.hpp>

namespace hfvrp {

namespace {

// Disposicao linear das variaveis de decisao:
//   x_{i,j,k}  i,j em 0..N e k em 0..M-1, com x_{i,i,k} fixado em 0
//   y_k        k em 0..M-1
//   z_{i,k}    i em 0..N e k em 0..M-1, com z_{0,k} fixado em 0
//   t_i        i em 0..N, com t_0 fixado em 0
struct Layout {
    int N, M;
    int nx, ny, nz, nt;
    int x_off, y_off, z_off, t_off;

    Layout(int N_, int M_) : N(N_), M(M_) {
        nx = (N + 1) * (N + 1) * M;
        ny = M;
        nz = (N + 1) * M;
        nt = N + 1;
        x_off = 0;
        y_off = x_off + nx;
        z_off = y_off + ny;
        t_off = z_off + nz;
    }
    int total() const { return nx + ny + nz + nt; }
    int x(int i, int j, int k) const { return x_off + (i * (N + 1) + j) * M + k; }
    int y(int k)             const { return y_off + k; }
    int z(int i, int k)      const { return z_off + i * M + k; }
    int t(int i)             const { return t_off + i; }
};

Solution extract_solution(const double* primal, const Layout& L, const Instance& inst) {
    Solution sol;
    sol.routes.assign(inst.num_vehicles, {});

    for (int k = 0; k < inst.num_vehicles; ++k) {
        if (primal[L.y(k)] < 0.5) continue;
        sol.routes[k].vehicle_id = k;

        int current = 0;
        while (true) {
            int next = -1;
            for (int j = 0; j <= inst.num_customers; ++j) {
                if (j == current) continue;
                if (primal[L.x(current, j, k)] > 0.5) { next = j; break; }
            }
            if (next <= 0) break;                  // back to depot
            sol.routes[k].customers.push_back(next);
            current = next;
            if ((int)sol.routes[k].customers.size() > inst.num_customers) break;
        }
    }
    return sol;
}

} // namespace

ExactResult solve_exact(const Instance& inst, double beta,
                        double time_limit_sec, long node_limit,
                        int threads, double mem_limit_mb) {
    (void)mem_limit_mb;  // CBC has no direct memory cap; see header for node_limit.
    const int N = inst.num_customers;
    const int M = inst.num_vehicles;
    const Layout L(N, M);

    const double BIG_M = inst.distance_bound() + 1.0;

    // Colunas.
    const int ncols = L.total();
    std::vector<double> col_lb(ncols, 0.0), col_ub(ncols, 1.0), obj(ncols, 0.0);
    std::vector<char>   is_int(ncols, 1);

    // x_{i,j,k}: binaria, custo V_k * D_ij; a diagonal fica fixada em 0.
    for (int i = 0; i <= N; ++i)
        for (int j = 0; j <= N; ++j)
            for (int k = 0; k < M; ++k) {
                int c = L.x(i, j, k);
                if (i == j) { col_ub[c] = 0.0; continue; }
                obj[c] = inst.vehicles[k].variable_cost * inst.distance[i][j];
            }

    // y_k: binaria, custo fixo F_k.
    for (int k = 0; k < M; ++k) obj[L.y(k)] = inst.vehicles[k].fixed_cost;

    // z_{i,k}: binaria, com z_{0,k} fixado em 0.
    for (int k = 0; k < M; ++k) col_ub[L.z(0, k)] = 0.0;

    // t_i: continua em [0, BIG_M], com t_0 fixado em 0.
    for (int i = 0; i <= N; ++i) {
        int c = L.t(i);
        is_int[c] = 0;
        col_lb[c] = 0.0;
        col_ub[c] = (i == 0) ? 0.0 : BIG_M;
        obj[c] = beta * inst.priority[i];
    }

    // Montagem da matriz, linha a linha.
    CoinPackedMatrix mat(false, 0, 0);
    mat.setDimensions(0, ncols);
    std::vector<double> row_lb, row_ub;

    auto add_row = [&](const std::vector<int>& idx,
                       const std::vector<double>& coef,
                       double lb, double ub) {
        CoinPackedVector r((int)idx.size(), idx.data(), coef.data());
        mat.appendRow(r);
        row_lb.push_back(lb);
        row_ub.push_back(ub);
    };

    const double INF = 1.0e20;

    // (1) Cada cliente e atendido por exatamente um veiculo.
    for (int i = 1; i <= N; ++i) {
        std::vector<int> idx; std::vector<double> coef;
        for (int k = 0; k < M; ++k) { idx.push_back(L.z(i, k)); coef.push_back(1.0); }
        add_row(idx, coef, 1.0, 1.0);
    }

    // (2) Liga os arcos de saida a atribuicao.
    for (int i = 1; i <= N; ++i)
        for (int k = 0; k < M; ++k) {
            std::vector<int> idx; std::vector<double> coef;
            for (int j = 0; j <= N; ++j) if (j != i) {
                idx.push_back(L.x(i, j, k)); coef.push_back(1.0);
            }
            idx.push_back(L.z(i, k)); coef.push_back(-1.0);
            add_row(idx, coef, 0.0, 0.0);
        }

    // (3) Liga os arcos de chegada a atribuicao.
    for (int j = 1; j <= N; ++j)
        for (int k = 0; k < M; ++k) {
            std::vector<int> idx; std::vector<double> coef;
            for (int i = 0; i <= N; ++i) if (i != j) {
                idx.push_back(L.x(i, j, k)); coef.push_back(1.0);
            }
            idx.push_back(L.z(j, k)); coef.push_back(-1.0);
            add_row(idx, coef, 0.0, 0.0);
        }

    // (4) O veiculo deixa o deposito se e somente se for utilizado.
    for (int k = 0; k < M; ++k) {
        std::vector<int> idx; std::vector<double> coef;
        for (int j = 1; j <= N; ++j) { idx.push_back(L.x(0, j, k)); coef.push_back(1.0); }
        idx.push_back(L.y(k)); coef.push_back(-1.0);
        add_row(idx, coef, 0.0, 0.0);
    }

    // (5) E retorna ao deposito nas mesmas condicoes.
    for (int k = 0; k < M; ++k) {
        std::vector<int> idx; std::vector<double> coef;
        for (int i = 1; i <= N; ++i) { idx.push_back(L.x(i, 0, k)); coef.push_back(1.0); }
        idx.push_back(L.y(k)); coef.push_back(-1.0);
        add_row(idx, coef, 0.0, 0.0);
    }

    // (6) Capacidade do veiculo.
    for (int k = 0; k < M; ++k) {
        std::vector<int> idx; std::vector<double> coef;
        for (int i = 1; i <= N; ++i) {
            idx.push_back(L.z(i, k)); coef.push_back(inst.demand[i]);
        }
        idx.push_back(L.y(k)); coef.push_back(-inst.vehicles[k].capacity);
        add_row(idx, coef, -INF, 0.0);
    }

    // (7) MTZ, na forma linear padrao. Elimina subciclos e, ao mesmo tempo,
    //     faz de t_i a distancia acumulada ate a chegada em i:
    //         t_j - t_i - BIG_M * Somatorio_k x_{i,j,k} >= D_ij - BIG_M
    for (int i = 0; i <= N; ++i)
        for (int j = 1; j <= N; ++j) {
            if (i == j) continue;
            std::vector<int> idx; std::vector<double> coef;
            idx.push_back(L.t(j)); coef.push_back(1.0);
            if (i != 0) { idx.push_back(L.t(i)); coef.push_back(-1.0); }
            for (int k = 0; k < M; ++k) {
                idx.push_back(L.x(i, j, k)); coef.push_back(-BIG_M);
            }
            add_row(idx, coef, inst.distance[i][j] - BIG_M, INF);
        }

    // Carrega o modelo no Clp e resolve com o CBC.
    OsiClpSolverInterface osi;
    osi.messageHandler()->setLogLevel(0);
    osi.loadProblem(mat, col_lb.data(), col_ub.data(), obj.data(), row_lb.data(), row_ub.data());
    for (int c = 0; c < ncols; ++c) if (is_int[c]) osi.setInteger(c);
    osi.setObjSense(1.0);   // minimize

    CbcModel cbc(osi);
    cbc.messageHandler()->setLogLevel(0);
    cbc.setLogLevel(0);
    if (time_limit_sec > 0) cbc.setMaximumSeconds(time_limit_sec);
    if (node_limit     > 0) cbc.setMaximumNodes(node_limit);
    if (threads        > 0) cbc.setNumberThreads(threads);
    cbc.setAllowableGap(1e-6);

    Timer timer;
    cbc.initialSolve();
    // Valor da relaxacao linear no no raiz, antes da ramificacao.
    const double root_lp = cbc.solver()->getObjValue();
    cbc.branchAndBound();
    const double runtime = timer.seconds();

    ExactResult res;
    res.runtime_sec    = runtime;
    res.root_lp_bound  = root_lp;
    res.num_nodes      = cbc.getNodeCount();
    res.num_iterations = cbc.getIterationCount();
    res.num_solutions  = cbc.getSolutionCount();

    const bool has_solution = cbc.bestSolution() != nullptr;
    res.feasible_found = has_solution;
    res.optimal        = cbc.isProvenOptimal();
    res.lower_bound    = cbc.getBestPossibleObjValue();

    if (has_solution) {
        const double* primal = cbc.bestSolution();
        res.objective = cbc.getObjValue();
        res.solution  = extract_solution(primal, L, inst);
        evaluate(res.solution, inst, beta);
        const double denom = std::max(1e-10, std::abs(res.objective));
        res.gap = std::max(0.0, (res.objective - res.lower_bound) / denom);
        if (res.gap > 1.0) res.gap = 1.0;
    } else {
        res.gap = 1.0;
    }

    // Situacao de encerramento.
    if (res.optimal)                             res.status = "optimal";
    else if (cbc.isProvenInfeasible())           res.status = "infeasible";
    else if (cbc.isContinuousUnbounded())        res.status = "unbounded";
    else if (cbc.isNodeLimitReached())           res.status = "node_limit";
    else if (cbc.isSecondsLimitReached())        res.status = has_solution ? "time_limit" : "no_solution";
    else if (cbc.isAbandoned())                  res.status = "abandoned";
    else if (!has_solution)                      res.status = "no_solution";
    else                                         res.status = "unknown";

    return res;
}

} // namespace hfvrp
