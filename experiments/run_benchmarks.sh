#!/usr/bin/env bash
# Executa todos os metodos sobre todas as instancias e reune os resultados
# em um CSV.
#
# Uso:
#   experiments/run_benchmarks.sh [BETA] [LIMITE_DE_TEMPO]
#
# BETA vale 1.0 por padrao e o limite de tempo, 60 segundos. As sementes das
# meta-heuristicas vem da variavel SEEDS, separadas por espaco.
#
# Instancias com N ate VISUAL_MAX_N tambem geram um SVG por metodo em
# results/svg/. VISUAL_MAX_N=0 desativa.

set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BIN="$ROOT/hfvrp"
# Tudo o que a execucao produz fica sob RESULTS_DIR, de modo que uma
# varredura mantenha um diretorio por beta.
RESULTS_DIR="${RESULTS_DIR:-$ROOT/output}"
OUT="$RESULTS_DIR/results.csv"
SVG_DIR="$RESULTS_DIR/svg"
BETA="${1:-1.0}"
TIME_LIMIT="${2:-60}"
VISUAL_MAX_N="${VISUAL_MAX_N:-20}"
EXACT_MAX_N="${EXACT_MAX_N:-0}" # 0 = run exact everywhere; >0 = skip it above N
# Descarta instancias acima deste N (0 mantem todas). Diferente de
# EXACT_MAX_N, que apenas pula o modelo exato, aqui a instancia sai da bateria
# por completo, o que permite testar o fluxo em segundos.
INSTANCE_MAX_N="${INSTANCE_MAX_N:-0}"

# Metodos a executar, separados por espaco. Restringir esta lista e o modo
# barato de repetir parte de uma bateria: como o modelo exato domina o custo
# computacional, METHODS="savings tabu ga" refaz apenas as heuristicas. Use um
# RESULTS_DIR novo e mescle depois com experiments/merge_batteries.py.
METHODS="${METHODS:-exact savings tabu ga}"
method_enabled() {
    local m
    for m in $METHODS; do [[ "$m" == "$1" ]] && return 0; done
    return 1
}
for m in $METHODS; do
    case "$m" in
        exact|savings|tabu|ga) ;;
        *) echo "error: unknown method '$m' in METHODS (valid: exact savings tabu ga)"; exit 1 ;;
    esac
done
NODE_LIMIT="${NODE_LIMIT:-0}"   # 0 = no cap (exact only, soft memory proxy)
THREADS="${THREADS:-0}"         # 0 = solver default (exact only)
MEM_LIMIT="${MEM_LIMIT:-0}"     # 0 = no cap; CPLEX-only hard cap in MB (exact only)
JOBS="${JOBS:-1}"               # 1 = serial; >1 = run that many instances in parallel
[[ "$JOBS" =~ ^[0-9]+$ ]] || JOBS=1
(( JOBS < 1 )) && JOBS=1

# Hiperparametros do AG; vazio mantem os padroes 80/300/0,10/3/0,10.
GA_POP="${GA_POP:-}"
GA_GENS="${GA_GENS:-}"
GA_MUT="${GA_MUT:-}"
GA_TOURNAMENT="${GA_TOURNAMENT:-}"
GA_ELITISM="${GA_ELITISM:-}"

GA_FLAGS=()
[[ -n "$GA_POP"        ]] && GA_FLAGS+=(--ga-population  "$GA_POP")
[[ -n "$GA_GENS"       ]] && GA_FLAGS+=(--ga-generations "$GA_GENS")
[[ -n "$GA_MUT"        ]] && GA_FLAGS+=(--ga-mutation    "$GA_MUT")
[[ -n "$GA_TOURNAMENT" ]] && GA_FLAGS+=(--ga-tournament  "$GA_TOURNAMENT")
[[ -n "$GA_ELITISM"    ]] && GA_FLAGS+=(--ga-elitism     "$GA_ELITISM")

# GA_CONFIGS compara varias configuracoes do AG numa mesma bateria. Formato:
# "NOME:chave=valor,chave=valor" separado por espacos, com as chaves pop,
# gens, mut, tournament e elitism. Exemplo:
#   GA_CONFIGS="baseline: small:pop=40,mut=0.15 high_mut:mut=0.30"
# Vazio executa uma unica configuracao, com rotulo de variante vazio.
GA_CONFIGS="${GA_CONFIGS:-}"
GA_VARIANT_NAMES=()
GA_VARIANT_FLAGS=()   # indexed in parallel with NAMES; each entry is a
                      # Cada entrada guarda a linha de opcoes ja montada,
                      # contornando a falta de arrays aninhados no bash.
if [[ -n "$GA_CONFIGS" ]]; then
    for tok in $GA_CONFIGS; do
        vname="${tok%%:*}"
        rest="${tok#*:}"
        [[ -z "$vname" ]] && { echo "skipping empty variant name in GA_CONFIGS"; continue; }
        vflags=""
        if [[ -n "$rest" && "$rest" != "$tok" ]]; then
            IFS=',' read -r -a pairs <<< "$rest"
            for p in "${pairs[@]}"; do
                [[ -z "$p" ]] && continue
                key="${p%%=*}"; val="${p#*=}"
                case "$key" in
                    pop)        vflags+=" --ga-population $val" ;;
                    gens)       vflags+=" --ga-generations $val" ;;
                    mut)        vflags+=" --ga-mutation $val" ;;
                    tournament) vflags+=" --ga-tournament $val" ;;
                    elitism)    vflags+=" --ga-elitism $val" ;;
                    *) echo "warning: unknown GA key '$key' in variant '$vname'" ;;
                esac
            done
        fi
        GA_VARIANT_NAMES+=("$vname")
        GA_VARIANT_FLAGS+=("$vflags")
    done
fi

# Le as sementes da variavel SEEDS, ou usa o padrao.
if [[ -n "${SEEDS:-}" ]]; then
    read -r -a SEEDS <<< "$SEEDS"
else
    SEEDS=(1 2 3 4 5)
fi
echo "Using ${#SEEDS[@]} seed(s): ${SEEDS[*]}"
echo "Writing to ${OUT}"
if [[ "$METHODS" != "exact savings tabu ga" ]]; then
    echo "Methods restricted to: ${METHODS}"
fi
echo "Auto-visual for N <= ${VISUAL_MAX_N} (SVGs in ${SVG_DIR#$ROOT/})"
if (( EXACT_MAX_N > 0 )); then
    echo "Exact restricted to N <= ${EXACT_MAX_N} (larger instances: heuristics only)."
fi
if (( NODE_LIMIT > 0 )); then
    echo "Exact runs capped at ${NODE_LIMIT} B&B nodes (memory proxy)."
fi
if (( THREADS > 0 )); then
    echo "Exact runs using ${THREADS} thread(s)."
fi
if [[ -n "$MEM_LIMIT" && "$MEM_LIMIT" != "0" ]]; then
    echo "Exact runs capped at ${MEM_LIMIT} MB (CPLEX only; CBC ignores)."
fi
if (( ${#GA_FLAGS[@]} > 0 )); then
    echo "GA overrides: ${GA_FLAGS[*]}"
fi
if (( ${#GA_VARIANT_NAMES[@]} > 0 )); then
    echo "GA variants (${#GA_VARIANT_NAMES[@]}):"
    for i in "${!GA_VARIANT_NAMES[@]}"; do
        echo "  - ${GA_VARIANT_NAMES[$i]} ⇒${GA_VARIANT_FLAGS[$i]:- (internal defaults)}"
    done
fi
if (( JOBS > 1 )); then
    echo "Parallel mode: up to ${JOBS} instance(s) at a time."
    if (( THREADS == 0 )); then
        echo "warning: JOBS=${JOBS} with THREADS=0 — the MILP solver will pick its own"
        echo "         thread count and may oversubscribe the CPU. Pass THREADS=1"
        echo "         (e.g. make benchmark JOBS=${JOBS} THREADS=1)."
    fi
fi

[[ -x "$BIN" ]] || { echo "Compile antes: make build"; exit 1; }
mkdir -p "$(dirname "$OUT")" "$SVG_DIR"
# Grava o cabecalho antes de comecar: sem isso, execucoes paralelas podem
# testar ao mesmo tempo se o arquivo esta vazio e escreve-lo duas vezes.
# O formato precisa acompanhar o de main.cpp.
printf '%s\n' \
    'method,instance,N,M,beta,seed,cost_operational,cost_priority,cost_total,lower_bound,root_lp_bound,gap,optimal,feasible,status,num_nodes,num_iterations,num_solutions,runtime_sec,variant' \
    > "$OUT"

shopt -s nullglob
# Primeiro as instancias de validacao, depois as sinteticas e por fim as
# convertidas da literatura.
INSTANCES=("$ROOT"/instances/tests/*.vrp
           "$ROOT"/instances/custom/*.vrp
           "$ROOT"/instances/benchmarks/*.vrp)
shopt -u nullglob
(( ${#INSTANCES[@]} == 0 )) && { echo "No instances found. Run 'make generate-instances' and/or 'make download-benchmarks'."; exit 1; }

# Le o numero de clientes do cabecalho CUSTOMERS. Devolve 0 se ilegivel, o
# que desativa o SVG daquela instancia.
instance_n() {
    local f="$1"
    awk -F: 'toupper($1) ~ /^[[:space:]]*CUSTOMERS[[:space:]]*$/ {
                gsub(/[^0-9]/, "", $2); print $2; exit
             }' "$f"
}

# Descarta as instancias grandes antes de comecar, para que a contagem
# exibida corresponda ao trabalho efetivamente feito.
if (( INSTANCE_MAX_N > 0 )); then
    kept=()
    for inst in "${INSTANCES[@]}"; do
        n="$(instance_n "$inst" 2>/dev/null || echo 0)"
        (( ${n:-0} > 0 && ${n:-0} <= INSTANCE_MAX_N )) && kept+=("$inst")
    done
    echo "Instance filter: N <= ${INSTANCE_MAX_N} — ${#kept[@]} of ${#INSTANCES[@]} instances kept."
    INSTANCES=("${kept[@]}")
    (( ${#INSTANCES[@]} == 0 )) && { echo "No instance has N <= ${INSTANCE_MAX_N}."; exit 1; }
fi

# Executa todos os metodos, sementes e variantes sobre uma instancia. Pode
# ser chamada em paralelo: cada linha do CSV e escrita em modo append, os
# nomes dos SVGs carregam o nome da instancia e o cabecalho ja foi gravado.
process_instance() {
    local inst="$1"
    local name n visual_on=0 exact_on=1
    name="$(basename "${inst%.vrp}")"
    n="$(instance_n "$inst" 2>/dev/null || echo 0)"
    n="${n:-0}"
    echo "=== $(basename "$inst")  (N=${n}) ==="

    if (( VISUAL_MAX_N > 0 && n > 0 && n <= VISUAL_MAX_N )); then
        visual_on=1
    fi
    # Acima de EXACT_MAX_N o modelo exato nao fecha: apenas consome o limite
    # de tempo. Nesses tamanhos ficam so as heuristicas.
    if (( EXACT_MAX_N > 0 && n > EXACT_MAX_N )); then
        exact_on=0
    fi

    run() {
        local method="$1"; local tag="$2"; shift 2
        local extra=()
        if (( visual_on )); then
            extra+=(--visual "${SVG_DIR}/${name}_${tag}.svg")
        fi
        if [[ "$method" == "exact" && "$NODE_LIMIT" -gt 0 ]]; then
            extra+=(--node-limit "$NODE_LIMIT")
        fi
        if [[ "$method" == "exact" && "$THREADS" -gt 0 ]]; then
            extra+=(--threads "$THREADS")
        fi
        if [[ "$method" == "exact" && -n "$MEM_LIMIT" && "$MEM_LIMIT" != "0" ]]; then
            extra+=(--mem-limit "$MEM_LIMIT")
        fi
        if [[ "$method" == "ga" && ${#GA_FLAGS[@]} -gt 0 ]]; then
            extra+=("${GA_FLAGS[@]}")
        fi
        "$BIN" --method "$method" --instance "$inst" --beta "$BETA" \
               --time-limit "$TIME_LIMIT" --output "$OUT" --quiet \
               "${extra[@]}" "$@" || true
    }

    if method_enabled exact; then
        if (( exact_on )); then
            run exact exact
        else
            echo "    (skipping exact: N=${n} > EXACT_MAX_N=${EXACT_MAX_N})"
        fi
    fi
    method_enabled savings && run savings savings
    # Enumerate SEEDS with an index so that repeated sentinels ("null",
    # "random", …) generate distinct SVG names (otherwise three back-to-back
    # "null" runs would all write to tabu_seednull.svg).
    local i s ls tag_seed vi vname vflags_str
    local -a vflags_arr
    if ! method_enabled tabu && ! method_enabled ga; then
        return 0
    fi
    for i in "${!SEEDS[@]}"; do
        s="${SEEDS[$i]}"
        ls="$(printf '%s' "$s" | tr '[:upper:]' '[:lower:]')"
        case "$ls" in
            null|random|rand|auto) tag_seed="auto${i}" ;;
            *)                     tag_seed="$s"       ;;
        esac
        method_enabled tabu && run tabu "tabu_seed${tag_seed}" --seed "$s"
        if ! method_enabled ga; then
            continue
        fi
        if (( ${#GA_VARIANT_NAMES[@]} == 0 )); then
            run ga "ga_seed${tag_seed}" --seed "$s"
        else
            # One ga run per variant per seed.  Each variant's flags override
            # any global GA_POP/GA_MUT/... (which are already in GA_FLAGS and
            # would be superseded later on the CLI by the variant's flags).
            for vi in "${!GA_VARIANT_NAMES[@]}"; do
                vname="${GA_VARIANT_NAMES[$vi]}"
                vflags_str="${GA_VARIANT_FLAGS[$vi]}"
                # shellcheck disable=SC2206
                vflags_arr=( $vflags_str )   # intentional word-splitting
                run ga "ga_${vname}_seed${tag_seed}" \
                    --seed "$s" --variant "$vname" "${vflags_arr[@]}"
            done
        fi
    done
}

if (( JOBS > 1 )); then
    # Parallel dispatcher.  Keep at most JOBS workers running at a time,
    # piping each worker's combined stdout/stderr through sed so every log
    # line carries the instance name — otherwise 5 concurrent solver runs
    # would scramble the output stream.
    active=0
    for inst in "${INSTANCES[@]}"; do
        inst_name="$(basename "${inst%.vrp}")"
        ( process_instance "$inst" 2>&1 | sed -u "s|^|[${inst_name}] |" ) &
        active=$((active + 1))
        if (( active >= JOBS )); then
            wait -n || true
            active=$((active - 1))
        fi
    done
    wait
else
    for inst in "${INSTANCES[@]}"; do
        process_instance "$inst"
    done
fi

echo "Results in $OUT"
(( VISUAL_MAX_N > 0 )) && echo "SVGs in $SVG_DIR"
