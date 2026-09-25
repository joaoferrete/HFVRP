#!/usr/bin/env bash
# Varredura de beta: uma bateria completa por valor, em sequencia, cada uma
# em seu proprio diretorio. Pensada para ser lancada uma vez no servidor e
# deixada rodando por dias:
#
#   nohup experiments/run_beta_sweep.sh > sweep.log 2>&1 &
#
# A execucao e retomavel: cada bateria concluida grava um marcador .done e e
# pulada na invocacao seguinte. Se a varredura for interrompida, basta repetir
# o mesmo comando. Para refazer um beta, apague o .done do diretorio dele.
#
# Todos os parametros abaixo aceitam sobrescrita pelo ambiente:
#   BETAS="0 1" TIME_LIMIT=3600 experiments/run_beta_sweep.sh

set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"

# ---------------------------------------------------------------------------
# Configuracao da varredura
# ---------------------------------------------------------------------------

# Grade de beta. O ponto em que as duas parcelas do objetivo se equilibram
# fica abaixo da unidade, de modo que a faixa informativa e beta < 1; dai a
# grade ser densa embaixo.
#   0      HFVRP puro, sem prioridade; caso de referencia
#   0.001  desempate lexicografico: mesmo otimo operacional de beta=0, mas a
#          prioridade escolhe qual das solucoes empatadas e devolvida
#   0.1    prioridade como ajuste fino, abaixo do ponto de equilibrio
#   0.25   proximo ao equilibrio das instancias maiores
#   0.5    proximo ao equilibrio mediano
#   1, 2   a prioridade domina o custo operacional
#   5      extremo, onde a fronteira de Pareto satura
BETAS="${BETAS:-0 0.001 0.1 0.25 0.5 1 2 5}"

# Destino de cada bateria: $SWEEP_ROOT/beta_<valor>/
SWEEP_ROOT="${SWEEP_ROOT:-$ROOT/output/sweep}"

# Resolvedor e limites de execucao.
SOLVER="${SOLVER:-cplex}"
TIME_LIMIT="${TIME_LIMIT:-18000}"
MEM_LIMIT="${MEM_LIMIT:-8192}"
THREADS="${THREADS:-1}"
JOBS="${JOBS:-5}"

# Tamanho maximo em que o modelo exato e executado (0 remove a restricao).
# Acima de N=30 ele nao prova otimalidade e apenas consome o limite de tempo;
# EXACT_MAX_N=20 o restringe aos tamanhos em que de fato fecha.
EXACT_MAX_N="${EXACT_MAX_N:-0}"
VISUAL_MAX_N="${VISUAL_MAX_N:-20}"

# Descarta instancias acima deste N (0 mantem todas). Util para ensaiar a
# varredura inteira em minutos antes de ocupar o servidor por dias:
#   INSTANCE_MAX_N=15 TIME_LIMIT=30 experiments/run_beta_sweep.sh
INSTANCE_MAX_N="${INSTANCE_MAX_N:-0}"

# Metodos executados em cada bateria. Como o modelo exato domina o custo
# computacional, repetir apenas as heuristicas custa uma fracao do total:
#   METHODS="savings tabu ga" SWEEP_ROOT=$PWD/results_heur \
#       experiments/run_beta_sweep.sh
# A mesclagem com a bateria anterior e feita depois:
#   python3 experiments/merge_batteries.py results results_heur -o results_final
METHODS="${METHODS:-exact savings tabu ga}"

SEEDS="${SEEDS:-null 1 10 50 75 100 200 999 1234 9999 19345 null}"
GA_CONFIGS="${GA_CONFIGS:-DEFAULT: SMALL:pop=50,mut=0.1,gens=50 MEDIUM:pop=500,mut=0.15,gens=1000 BIG:pop=1000,mut=0.2,gens=1500}"

# Recompila antes da primeira bateria. Use SKIP_BUILD=1 se o binario ja
# estiver atualizado.
SKIP_BUILD="${SKIP_BUILD:-0}"

# Executa o analyze.py ao fim de cada bateria, gravando tabelas e figuras ao
# lado do results.csv correspondente. Use 0 para coletar apenas os CSVs.
RUN_ANALYZE="${RUN_ANALYZE:-1}"

PY="${PY:-python3}"

# ---------------------------------------------------------------------------

stamp() { date '+%Y-%m-%d %H:%M:%S'; }
log()   { echo "[$(stamp)] $*"; }

log "Beta sweep starting"
log "  betas        : ${BETAS}"
log "  output root  : ${SWEEP_ROOT}"
log "  solver       : ${SOLVER}"
log "  time limit   : ${TIME_LIMIT}s per run"
log "  exact max N  : ${EXACT_MAX_N} (0 = no restriction)"
log "  methods      : ${METHODS}"
(( INSTANCE_MAX_N > 0 )) && log "  instance filter: only N <= ${INSTANCE_MAX_N}"
log "  jobs/threads : ${JOBS} instances in parallel, ${THREADS} solver thread(s)"
log "  seeds        : ${SEEDS}"
log "  GA variants  : ${GA_CONFIGS}"

# Fail fast on a missing instance bank rather than after the first build.
shopt -s nullglob
n_inst=$( { ls instances/tests/*.vrp instances/custom/*.vrp \
                instances/benchmarks/*.vrp; } 2>/dev/null | wc -l )
shopt -u nullglob
if (( n_inst == 0 )); then
    log "ERROR: no .vrp instances found."
    log "       Rode antes: make generate-instances e make download-benchmarks."
    exit 1
fi
if (( INSTANCE_MAX_N > 0 )); then
    log "  instances    : ${n_inst} found, filtered to N <= ${INSTANCE_MAX_N} per battery"
else
    log "  instances    : ${n_inst}"
fi

if [[ "$SKIP_BUILD" != "1" ]]; then
    log "Recompilando com SOLVER=${SOLVER} (SKIP_BUILD=1 para pular)"
    make rebuild SOLVER="$SOLVER" >/dev/null
    log "Build OK"
else
    log "Compilacao pulada (SKIP_BUILD=1)"
fi
[[ -x hfvrp ]] || { log "ERRO: binario hfvrp ausente apos a compilacao"; exit 1; }

mkdir -p "$SWEEP_ROOT"

total=0; done_already=0; failed=()
for beta in $BETAS; do
    total=$((total + 1))
    dir="$SWEEP_ROOT/beta_${beta}"

    if [[ -f "$dir/.done" ]]; then
        log "=== beta=${beta}: already complete (${dir}/.done) — skipping"
        done_already=$((done_already + 1))
        continue
    fi

    mkdir -p "$dir"
    log "=== beta=${beta}: starting  ->  ${dir}"
    started=$(date +%s)

    # Each battery gets its own log so a 4-day sweep stays readable.
    if make benchmark \
            BETA="$beta" \
            RESULTS_DIR="$dir" \
            SOLVER="$SOLVER" \
            TIME_LIMIT="$TIME_LIMIT" \
            MEM_LIMIT="$MEM_LIMIT" \
            THREADS="$THREADS" \
            JOBS="$JOBS" \
            EXACT_MAX_N="$EXACT_MAX_N" \
            VISUAL_MAX_N="$VISUAL_MAX_N" \
            INSTANCE_MAX_N="$INSTANCE_MAX_N" \
            METHODS="$METHODS" \
            SEEDS="$SEEDS" \
            GA_CONFIGS="$GA_CONFIGS" \
            > "$dir/benchmark.log" 2>&1
    then
        elapsed=$(( $(date +%s) - started ))
        rows=$(( $(wc -l < "$dir/results.csv") - 1 ))
        log "=== beta=${beta}: benchmark done in $((elapsed / 3600))h$(( (elapsed % 3600) / 60 ))m — ${rows} rows"

        if [[ "$RUN_ANALYZE" == "1" ]]; then
            if "$PY" experiments/analyze.py "$dir/results.csv" \
                    > "$dir/analyze.log" 2>&1; then
                log "=== beta=${beta}: analyze done (tables + figures in ${dir})"
            else
                log "=== beta=${beta}: WARNING analyze failed — see ${dir}/analyze.log"
            fi
        fi

        # Marker last, so an interrupted battery is retried rather than skipped.
        printf 'beta=%s\ncompleted=%s\nelapsed_sec=%s\nrows=%s\n' \
            "$beta" "$(stamp)" "$elapsed" "$rows" > "$dir/.done"
    else
        log "=== beta=${beta}: FAILED — see ${dir}/benchmark.log"
        failed+=("$beta")
    fi
done

log "Sweep finished: ${total} beta(s) requested, ${done_already} already complete, ${#failed[@]} failed"
if (( ${#failed[@]} > 0 )); then
    log "Failed betas: ${failed[*]}"
    log "Re-run the same command to retry them (completed ones are skipped)."
fi
log "Per-beta results under ${SWEEP_ROOT}/beta_*/results.csv"
log "Cross-beta comparison: ${PY} experiments/compare_betas.py ${SWEEP_ROOT}"
(( ${#failed[@]} > 0 )) && exit 1
exit 0
