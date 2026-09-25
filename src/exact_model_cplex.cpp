// Backend CPLEX, pela API Concert. Reproduz exatamente a disposicao de
// variaveis e as restricoes de src/exact_model.cpp, que usa o CBC: o modelo
// montado e o mesmo, muda apenas o resolvedor.
//
// Compilado quando o Makefile recebe SOLVER=cplex.

#include "exact_model.hpp"
#include "utils.hpp"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <vector>

// O Concert exige IL_STD para usar os iostreams de std::. O Makefile ja
// define o simbolo; a guarda abaixo permite compilar o arquivo isoladamente
// sem aviso de redefinicao.
#ifndef IL_STD
#define IL_STD
#endif
#include <ilcplex/ilocplex.h>

namespace hfvrp {

ExactResult solve_exact(const Instance& inst, double beta,
                        double time_limit_sec, long node_limit,
                        int threads, double mem_limit_mb) {
    const int N = inst.num_customers;
    const int M = inst.num_vehicles;
    const double BIG_M = inst.distance_bound() + 1.0;

    ExactResult res;
    IloEnv env;
    try {
        IloModel model(env);

        // x_{i,j,k} binaria, com a diagonal fixada em 0.
        IloArray<IloArray<IloBoolVarArray>> x(env, N + 1);
        for (int i = 0; i <= N; ++i) {
            x[i] = IloArray<IloBoolVarArray>(env, N + 1);
            for (int j = 0; j <= N; ++j) {
                x[i][j] = IloBoolVarArray(env, M);
                if (i == j)
                    for (int k = 0; k < M; ++k) x[i][j][k].setUB(0);
            }
        }

        // y_k binaria.
        IloBoolVarArray y(env, M);

        // z_{i,k} binaria, com z_{0,k} fixado em 0.
        IloArray<IloBoolVarArray> z(env, N + 1);
        for (int i = 0; i <= N; ++i) {
            z[i] = IloBoolVarArray(env, M);
            if (i == 0)
                for (int k = 0; k < M; ++k) z[i][k].setUB(0);
        }

        // t_i continua em [0, BIG_M], com t_0 fixado em 0.
        IloNumVarArray t(env, N + 1, 0.0, BIG_M, ILOFLOAT);
        t[0].setUB(0.0);

        // Guarda as variaveis inteiras para montar depois a relaxacao
        // linear e obter o limite da raiz, que o CPLEX nao expoe direto.
        IloNumVarArray int_vars(env);
        for (int i = 0; i <= N; ++i)
            for (int j = 0; j <= N; ++j)
                for (int k = 0; k < M; ++k)
                    int_vars.add(x[i][j][k]);
        for (int k = 0; k < M; ++k) int_vars.add(y[k]);
        for (int i = 0; i <= N; ++i)
            for (int k = 0; k < M; ++k) int_vars.add(z[i][k]);

        // Funcao objetivo.
        IloExpr obj(env);
        for (int i = 0; i <= N; ++i)
            for (int j = 0; j <= N; ++j) {
                if (i == j) continue;
                for (int k = 0; k < M; ++k)
                    obj += inst.vehicles[k].variable_cost * inst.distance[i][j] * x[i][j][k];
            }
        for (int k = 0; k < M; ++k) obj += inst.vehicles[k].fixed_cost * y[k];
        for (int i = 1; i <= N; ++i) obj += beta * inst.priority[i] * t[i];
        model.add(IloMinimize(env, obj));
        obj.end();

        // (1) Cada cliente e atendido por exatamente um veiculo.
        for (int i = 1; i <= N; ++i) {
            IloExpr e(env);
            for (int k = 0; k < M; ++k) e += z[i][k];
            model.add(e == 1);
            e.end();
        }

        // (2) Liga os arcos de saida a atribuicao.
        for (int i = 1; i <= N; ++i)
            for (int k = 0; k < M; ++k) {
                IloExpr e(env);
                for (int j = 0; j <= N; ++j) if (j != i) e += x[i][j][k];
                model.add(e == z[i][k]);
                e.end();
            }

        // (3) Liga os arcos de chegada a atribuicao.
        for (int j = 1; j <= N; ++j)
            for (int k = 0; k < M; ++k) {
                IloExpr e(env);
                for (int i = 0; i <= N; ++i) if (i != j) e += x[i][j][k];
                model.add(e == z[j][k]);
                e.end();
            }

        // (4) O veiculo deixa o deposito se e somente se for utilizado.
        for (int k = 0; k < M; ++k) {
            IloExpr e(env);
            for (int j = 1; j <= N; ++j) e += x[0][j][k];
            model.add(e == y[k]);
            e.end();
        }

        // (5) E retorna ao deposito nas mesmas condicoes.
        for (int k = 0; k < M; ++k) {
            IloExpr e(env);
            for (int i = 1; i <= N; ++i) e += x[i][0][k];
            model.add(e == y[k]);
            e.end();
        }

        // (6) Capacidade do veiculo.
        for (int k = 0; k < M; ++k) {
            IloExpr e(env);
            for (int i = 1; i <= N; ++i) e += inst.demand[i] * z[i][k];
            model.add(e <= inst.vehicles[k].capacity * y[k]);
            e.end();
        }

        // (7) MTZ. Com o arco em uso a restricao vira t_j >= t_i + D_ij;
        //     com o arco ocioso o termo BIG_M a torna inativa.
        for (int i = 0; i <= N; ++i)
            for (int j = 1; j <= N; ++j) {
                if (i == j) continue;
                IloExpr sumx(env);
                for (int k = 0; k < M; ++k) sumx += x[i][j][k];
                IloExpr lhs(env);
                lhs += t[j];
                if (i != 0) lhs -= t[i];
                lhs -= BIG_M * sumx;
                model.add(lhs >= inst.distance[i][j] - BIG_M);
                lhs.end();
                sumx.end();
            }

        // Limite da raiz obtido de uma relaxacao a parte: mesmo modelo, com
        // as variaveis inteiras convertidas em continuas.
        double root_lp = 0.0;
        try {
            IloModel relaxed(env);
            relaxed.add(model);
            relaxed.add(IloConversion(env, int_vars, ILOFLOAT));
            IloCplex lp(relaxed);
            lp.setOut(env.getNullStream());
            lp.setWarning(env.getNullStream());
            if (lp.solve()) root_lp = lp.getObjValue();
            lp.end();
        } catch (...) {
            root_lp = 0.0;
        }

        IloCplex cplex(model);
        cplex.setOut(env.getNullStream());
        cplex.setWarning(env.getNullStream());
        if (time_limit_sec > 0)
            cplex.setParam(IloCplex::Param::TimeLimit, time_limit_sec);
        if (node_limit > 0)
            cplex.setParam(IloCplex::Param::MIP::Limits::Nodes, (IloInt)node_limit);
        if (threads > 0)
            cplex.setParam(IloCplex::Param::Threads, (IloInt)threads);
        if (mem_limit_mb > 0.0) {
            // WorkMem e o limite a partir do qual o CPLEX passa a gravar nos
            // em disco; TreeMemory e o teto rigido, que aborta a execucao.
            // Ambos recebem o mesmo valor para que o encerramento seja limpo
            // em servidores com memoria restrita.
            cplex.setParam(IloCplex::Param::WorkMem, mem_limit_mb);
            cplex.setParam(IloCplex::Param::MIP::Limits::TreeMemory, mem_limit_mb);
        }
        cplex.setParam(IloCplex::Param::MIP::Tolerances::MIPGap, 1e-6);

        Timer timer;
        const bool ok = cplex.solve();
        const double runtime = timer.seconds();

        res.runtime_sec    = runtime;
        res.root_lp_bound  = root_lp;
        res.num_nodes      = (long)cplex.getNnodes();
        res.num_iterations = (long)cplex.getNiterations();
        res.num_solutions  = cplex.getSolnPoolNsolns();

        const auto alg_status = cplex.getStatus();
        const auto cpx_status = cplex.getCplexStatus();
        const bool has_solution = ok && cplex.isPrimalFeasible();

        res.feasible_found = has_solution;
        res.optimal        = (alg_status == IloAlgorithm::Optimal);

        try { res.lower_bound = cplex.getBestObjValue(); }
        catch (...) { res.lower_bound = 0.0; }

        if (has_solution) {
            res.objective = cplex.getObjValue();
            Solution sol;
            sol.routes.assign(M, {});
            for (int k = 0; k < M; ++k) {
                if (cplex.getValue(y[k]) < 0.5) continue;
                sol.routes[k].vehicle_id = k;
                int current = 0;
                while (true) {
                    int next = -1;
                    for (int j = 0; j <= N; ++j) {
                        if (j == current) continue;
                        if (cplex.getValue(x[current][j][k]) > 0.5) { next = j; break; }
                    }
                    if (next <= 0) break;
                    sol.routes[k].customers.push_back(next);
                    current = next;
                    if ((int)sol.routes[k].customers.size() > N) break;
                }
            }
            res.solution = sol;
            evaluate(res.solution, inst, beta);
            const double denom = std::max(1e-10, std::abs(res.objective));
            res.gap = std::max(0.0, (res.objective - res.lower_bound) / denom);
            if (res.gap > 1.0) res.gap = 1.0;
        } else {
            res.gap = 1.0;
        }

        if (res.optimal)                                        res.status = "optimal";
        else if (alg_status == IloAlgorithm::Infeasible)        res.status = "infeasible";
        else if (alg_status == IloAlgorithm::Unbounded)         res.status = "unbounded";
        else if (cpx_status == IloCplex::AbortTimeLim ||
                 cpx_status == IloCplex::AbortDetTimeLim)
            res.status = has_solution ? "time_limit" : "no_solution";
        else if (cpx_status == IloCplex::NodeLimFeas)           res.status = "node_limit";
        else if (cpx_status == IloCplex::NodeLimInfeas)         res.status = "node_limit";
        else if (cpx_status == IloCplex::MemLimFeas ||
                 cpx_status == IloCplex::MemLimInfeas)
            res.status = has_solution ? "mem_limit" : "no_solution";
        else if (!has_solution)                                 res.status = "no_solution";
        else                                                    res.status = "unknown";

    } catch (IloException& ex) {
        std::cerr << "CPLEX exception: " << ex.getMessage() << std::endl;
        res.status = "abandoned";
        res.gap    = 1.0;
    } catch (std::exception& ex) {
        std::cerr << "exception: " << ex.what() << std::endl;
        res.status = "abandoned";
        res.gap    = 1.0;
    }
    env.end();
    return res;
}

} // namespace hfvrp
