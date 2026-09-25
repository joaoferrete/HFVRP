#include "instance.hpp"
#include "solution.hpp"
#include "exact_model.hpp"
#include "savings.hpp"
#include "tabu.hpp"
#include "genetic.hpp"
#include "utils.hpp"
#include "visual.hpp"

#include <cctype>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <string>

using namespace hfvrp;

namespace {

struct Args {
    std::string method;
    std::string instance_path;
    std::string output_csv;
    std::string visual_path;    // empty => disabled; "auto" => default under results/
    std::uint64_t seed = 42;
    bool seed_is_auto = false;   // true when user passed --seed null|random|rand|auto
    double beta = 1.0;
    double time_limit = 60.0;
    long node_limit = 0;        // 0 = no cap; only honoured by --method exact
    int    threads       = 0;   // 0 = solver default; only honoured by --method exact
    double mem_limit_mb  = 0.0; // 0 = no cap; CPLEX honours it (TreeMemory), CBC ignores
    bool verbose = false;
    bool quiet = false;

    // Parametros do AG; valores negativos mantem o padrao interno.
    int    ga_population   = -1;
    int    ga_generations  = -1;
    double ga_mutation     = -1.0;
    int    ga_tournament   = -1;
    double ga_elitism      = -1.0;

    // Rotulo livre, gravado na coluna "variant" do CSV. Permite distinguir
    // varias configuracoes do mesmo metodo numa unica bateria.
    std::string variant;
};

void usage() {
    std::cout <<
        "Uso:\n"
        "  hfvrp --method {exact|savings|tabu|ga} --instance CAMINHO [opcoes]\n"
        "Obrigatorias:\n"
        "  --method M         metodo de solucao: exact, savings, tabu ou ga\n"
        "  --instance CAMINHO arquivo .vrp da instancia a resolver\n"
        "Opcoes:\n"
        "  -h, --help         mostra esta ajuda e encerra\n"
        "  --seed N           semente das meta-heuristicas (padrao 42). Use\n"
        "                     'null', 'random', 'rand' ou 'auto' para sortear\n"
        "                     a semente pelo relogio; o valor sorteado e\n"
        "                     impresso e gravado no CSV.\n"
        "  --beta F           peso da prioridade (padrao 1.0)\n"
        "  --time-limit S     limite de tempo em segundos (padrao 60)\n"
        "  --node-limit N     teto de nos de branch-and-bound (so no exato;\n"
        "                     0 desativa). Serve como aproximacao de limite\n"
        "                     de memoria, ja que os nos abertos dominam a RAM.\n"
        "  --threads N        threads do resolvedor (so no exato; 0 usa o\n"
        "                     padrao, em geral todos os nucleos).\n"
        "  --mem-limit MB     teto de memoria do resolvedor (so no exato;\n"
        "                     0 desativa). Respeitado pelo CPLEX e ignorado\n"
        "                     pelo CBC, que aceita apenas --node-limit.\n"
        "  --output CSV       acrescenta uma linha ao arquivo CSV\n"
        "  --visual [CAMINHO] desenha as rotas em SVG; sem CAMINHO, grava em\n"
        "                     output/<instancia>_<metodo>.svg\n"
        "  --verbose          imprime as rotas\n"
        "  --quiet            omite a linha de resumo method=...\n"
        "  --ga-population N  tamanho da populacao do AG (padrao 80)\n"
        "  --ga-generations N numero maximo de geracoes do AG (padrao 300)\n"
        "  --ga-mutation F    taxa de mutacao por filho (padrao 0.10)\n"
        "  --ga-tournament N  tamanho do torneio (padrao 3)\n"
        "  --ga-elitism F     fracao de elite preservada (padrao 0.10)\n"
        "  --variant NOME     rotulo gravado na coluna 'variant' do CSV,\n"
        "                     para distinguir configuracoes de um mesmo metodo\n";
}

bool parse_args(int argc, char** argv, Args& a) {
    for (int i = 1; i < argc; ++i) {
        std::string k = argv[i];
        auto peek = [&]() -> const char* {
            return (i + 1 < argc) ? argv[i + 1] : nullptr;
        };
        auto next = [&]() -> std::string {
            if (i + 1 >= argc) throw std::runtime_error("missing value for " + k);
            return argv[++i];
        };
        if (k == "--method")       a.method = next();
        else if (k == "--instance")a.instance_path = next();
        else if (k == "--seed") {
            std::string v = next();
            std::string lv = v;
            for (auto& c : lv) c = (char)std::tolower((unsigned char)c);
            if (lv == "null" || lv == "random" || lv == "rand" || lv == "auto") {
                a.seed_is_auto = true;   // resolved right before dispatch
            } else {
                a.seed = std::stoull(v);
                a.seed_is_auto = false;
            }
        }
        else if (k == "--beta")    a.beta = std::stod(next());
        else if (k == "--time-limit") a.time_limit = std::stod(next());
        else if (k == "--node-limit") a.node_limit = std::stol(next());
        else if (k == "--threads")    a.threads    = std::stoi(next());
        else if (k == "--mem-limit")  a.mem_limit_mb = std::stod(next());
        else if (k == "--output")  a.output_csv = next();
        else if (k == "--visual") {
            // Aceita um caminho se o argumento seguinte nao for outra opcao.
            const char* p = peek();
            if (p && p[0] != '-') a.visual_path = next();
            else                  a.visual_path = "auto";
        }
        else if (k == "--ga-population")  a.ga_population  = std::stoi(next());
        else if (k == "--ga-generations") a.ga_generations = std::stoi(next());
        else if (k == "--ga-mutation")    a.ga_mutation    = std::stod(next());
        else if (k == "--ga-tournament")  a.ga_tournament  = std::stoi(next());
        else if (k == "--ga-elitism")     a.ga_elitism     = std::stod(next());
        else if (k == "--variant")        a.variant        = next();
        else if (k == "--verbose" || k == "-v") a.verbose = true;
        else if (k == "--quiet"   || k == "-q") a.quiet = true;
        else if (k == "--help" || k == "-h") { usage(); return false; }
        else throw std::runtime_error("unknown arg: " + k);
    }
    return !a.method.empty() && !a.instance_path.empty();
}

std::string default_visual_path(const Args& a, const std::string& instance_name) {
    return "results/" + instance_name + "_" + a.method + ".svg";
}

// Colunas preenchidas apenas pelo modelo exato; as heuristicas as deixam vazias.
struct ExactExtras {
    bool   present        = false;
    double root_lp_bound  = 0.0;
    double gap            = 0.0;
    long   num_nodes      = 0;
    long   num_iterations = 0;
    int    num_solutions  = 0;
    std::string status;   // "" when not an exact run
};

void append_csv(const std::string& path, const std::string& method,
                const Instance& inst, const Solution& sol, double beta,
                double runtime, double lower_bound, bool optimal,
                std::uint64_t seed, const ExactExtras& ex,
                const std::string& variant) {
    // Escreve o cabecalho quando o arquivo ainda nao existe, esta vazio ou
    // contem apenas espacos.
    bool needs_header = true;
    {
        std::ifstream probe(path);
        if (probe.good()) {
            probe.seekg(0, std::ios::end);
            needs_header = (probe.tellg() <= 0);
        }
    }
    std::ofstream out(path, std::ios::app);
    if (!out) throw std::runtime_error("cannot write CSV: " + path);
    if (needs_header) {
        out << "method,instance,N,M,beta,seed,cost_operational,cost_priority,"
               "cost_total,lower_bound,root_lp_bound,gap,optimal,feasible,"
               "status,num_nodes,num_iterations,num_solutions,runtime_sec,"
               "variant\n";
    }
    out << std::fixed << std::setprecision(6);
    out << method << ',' << inst.name << ',' << inst.num_customers << ','
        << inst.num_vehicles << ',' << beta << ',' << seed << ','
        << sol.cost_operational << ',' << sol.cost_priority << ','
        << sol.cost_total << ',' << lower_bound << ',';
    if (ex.present) out << ex.root_lp_bound << ',' << ex.gap << ',';
    else            out << ",,";
    out << (optimal ? 1 : 0) << ',' << (sol.feasible ? 1 : 0) << ',';
    if (ex.present) out << ex.status << ',' << ex.num_nodes << ','
                        << ex.num_iterations << ',' << ex.num_solutions << ',';
    else            out << ",,,,";
    out << runtime << ',' << variant << '\n';
}

} // namespace

int main(int argc, char** argv) try {
    Args a;
    if (!parse_args(argc, argv, a)) { usage(); return 1; }

    Instance inst = load_instance(a.instance_path);
    if (a.seed_is_auto) {
        a.seed = auto_seed();
        if (!a.quiet) std::cerr << "seed (auto) = " << a.seed << '\n';
    }
    Solution sol;
    double runtime = 0.0, lower_bound = 0.0;
    bool optimal = false;
    ExactExtras ex;

    Timer t;
    if (a.method == "exact") {
        ExactResult r = solve_exact(inst, a.beta, a.time_limit, a.node_limit,
                                    a.threads, a.mem_limit_mb);
        sol = r.solution;
        runtime = r.runtime_sec;
        lower_bound = r.lower_bound;
        optimal = r.optimal;
        ex.present        = true;
        ex.root_lp_bound  = r.root_lp_bound;
        ex.gap            = r.gap;
        ex.num_nodes      = r.num_nodes;
        ex.num_iterations = r.num_iterations;
        ex.num_solutions  = r.num_solutions;
        ex.status         = r.status;
    } else if (a.method == "savings") {
        sol = clarke_wright(inst, a.beta);
        runtime = t.seconds();
    } else if (a.method == "tabu") {
        TabuParams p; p.time_limit_sec = a.time_limit;
        sol = tabu_search(inst, a.beta, p, a.seed);
        runtime = t.seconds();
    } else if (a.method == "ga") {
        GAParams p;
        p.time_limit_sec = a.time_limit;
        if (a.ga_population  >  0)   p.population_size  = a.ga_population;
        if (a.ga_generations >  0)   p.max_generations  = a.ga_generations;
        if (a.ga_mutation    >= 0.0) p.mutation_rate    = a.ga_mutation;
        if (a.ga_tournament  >  0)   p.tournament_size  = a.ga_tournament;
        if (a.ga_elitism     >= 0.0) p.elitism_fraction = a.ga_elitism;
        sol = genetic_algorithm(inst, a.beta, p, a.seed);
        runtime = t.seconds();
    } else {
        std::cerr << "unknown method: " << a.method << "\n";
        return 1;
    }

    if (!a.quiet) {
        std::cout << std::fixed << std::setprecision(3)
                  << "method=" << a.method
                  << " instance=" << inst.name
                  << " cost_total=" << sol.cost_total
                  << " runtime_sec=" << runtime
                  << " feasible=" << (sol.feasible ? "yes" : "no")
                  << (a.method == "exact" ?
                       (" lower_bound=" + std::to_string(lower_bound) +
                        (optimal ? " optimal" : " time_limit"))
                       : "")
                  << '\n';
    }
    if (a.verbose) print_solution(sol, inst, a.beta);
    if (!a.output_csv.empty())
        append_csv(a.output_csv, a.method, inst, sol, a.beta,
                   runtime, lower_bound, optimal, a.seed, ex, a.variant);
    if (!a.visual_path.empty()) {
        const std::string vp = (a.visual_path == "auto")
                                   ? default_visual_path(a, inst.name)
                                   : a.visual_path;
        VisualExtras vex;
        vex.runtime_sec = runtime;
        if (ex.present) {
            vex.exact         = true;
            vex.status        = ex.status;
            vex.lower_bound   = lower_bound;
            vex.root_lp_bound = ex.root_lp_bound;
            vex.gap           = ex.gap;
            vex.num_nodes     = ex.num_nodes;
            vex.optimal       = optimal;
        }
        write_svg(inst, sol, a.beta, a.method, vp, vex);
        std::cerr << "visual SVG: " << vp << "\n";
    }
    return sol.feasible ? 0 : 2;
} catch (std::exception& e) {
    std::cerr << "error: " << e.what() << '\n';
    return 3;
}
