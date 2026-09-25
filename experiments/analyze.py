#!/usr/bin/env python3
"""Tabelas e figuras a partir de um results.csv.

Uso:
    python3 experiments/analyze.py [results.csv]

Grava, no mesmo diretorio do CSV de entrada:
    summary.csv              custo e tempo medios por metodo e instancia
    summary_by_size.csv      gap e tempo medios por metodo e tamanho
    heuristic_pairwise.csv   confronto direto entre as heuristicas
    seed_efficiency.csv      ganho esperado do melhor entre k sementes
    ga_variants_summary.csv  estatisticas por variante do AG
    ga_variants_pairwise.csv confronto entre variantes do AG
    ga_variants_by_seed.csv  matriz de variante por semente
    delta_to_exact.csv       diferenca de custo contra o modelo exato
    feasibility_by_size.csv  taxa de viabilidade por metodo e tamanho
    fleet_by_size.csv        veiculos utilizados, lidos dos SVGs

As figuras correspondentes sao gravadas em PNG e PDF quando o matplotlib
esta disponivel.
"""

from __future__ import annotations
import csv
import os
import math
import pathlib
import re
import statistics
import sys
from collections import defaultdict

try:
    import matplotlib
    matplotlib.use("Agg")
    import matplotlib.pyplot as plt
except ImportError:
    plt = None

# Figuras são salvas uma vez por formato: PDF porque a monografia as inclui com
# \includegraphics e um raster amplia mal na impressão, PNG para inspeção rápida.
# Sobrescreva com FIG_FORMATS=pdf (ou png) se quiser apenas um.
FIG_EXTS = tuple(e.strip() for e in
                 (os.environ.get("FIG_FORMATS") or "png,pdf").split(",") if e.strip())


def save_fig(stem, fig=None, **kw):
    target = fig if fig is not None else plt
    for ext in FIG_EXTS:
        target.savefig(f"{stem}.{ext}", **kw)



def load(csv_path: pathlib.Path) -> list[dict]:
    rows = []
    with csv_path.open() as f:
        reader = csv.DictReader(f)
        for row in reader:
            row["N"] = int(row["N"])
            row["M"] = int(row["M"])
            row["seed"] = int(row["seed"])
            for k in ("beta", "cost_operational", "cost_priority",
                      "cost_total", "lower_bound", "runtime_sec"):
                row[k] = float(row[k])
            row["optimal"] = bool(int(row["optimal"]))
            row["feasible"] = bool(int(row["feasible"]))
            # A coluna `variant` e opcional.
            row["variant"] = (row.get("variant") or "").strip()
            rows.append(row)
    return rows


def summarize(rows: list[dict], out_dir: pathlib.Path) -> None:
    # (metodo, instancia) -> lista de custo e tempo
    by_mi: dict[tuple, list[tuple[float, float]]] = defaultdict(list)
    for r in rows:
        if not r["feasible"]:
            continue
        by_mi[(r["method"], r["instance"])].append((r["cost_total"], r["runtime_sec"]))

    with (out_dir / "summary.csv").open("w", newline="") as f:
        w = csv.writer(f)
        w.writerow(["method", "instance", "runs",
                    "cost_mean", "cost_std", "runtime_mean"])
        for (m, inst), vals in sorted(by_mi.items()):
            costs = [c for c, _ in vals]
            times = [t for _, t in vals]
            w.writerow([m, inst, len(vals),
                        f"{statistics.mean(costs):.4f}",
                        f"{statistics.pstdev(costs):.4f}" if len(costs) > 1 else "0",
                        f"{statistics.mean(times):.4f}"])

    # Gap contra o exato, quando houver: a referencia e o melhor custo dele.
    exact_best: dict[str, float] = {}
    for r in rows:
        if r["method"] == "exact" and r["feasible"]:
            exact_best[r["instance"]] = min(
                exact_best.get(r["instance"], r["cost_total"]), r["cost_total"])

    by_mn: dict[tuple, list[tuple[float, float, float]]] = defaultdict(list)
    for r in rows:
        if not r["feasible"]:
            continue
        ref = exact_best.get(r["instance"])
        gap = (r["cost_total"] - ref) / ref * 100 if ref else float("nan")
        by_mn[(r["method"], r["N"])].append((r["cost_total"], r["runtime_sec"], gap))

    with (out_dir / "summary_by_size.csv").open("w", newline="") as f:
        w = csv.writer(f)
        w.writerow(["method", "N", "runs",
                    "cost_mean", "cost_std",
                    "gap_mean_pct", "gap_std_pct",
                    "runtime_mean", "runtime_std"])
        for (m, N), vals in sorted(by_mn.items()):
            costs = [c for c, _, _ in vals]
            times = [t for _, t, _ in vals]
            gaps  = [g for _, _, g in vals if g == g]  # drop NaN
            def mean(xs): return statistics.mean(xs) if xs else float("nan")
            def std(xs):
                return statistics.pstdev(xs) if len(xs) > 1 else 0.0
            w.writerow([m, N, len(vals),
                        f"{mean(costs):.4f}",
                        f"{std(costs):.4f}",
                        f"{mean(gaps):.4f}"  if gaps  else "",
                        f"{std(gaps):.4f}"   if gaps  else "",
                        f"{mean(times):.4f}",
                        f"{std(times):.4f}"])


HEURISTICS = ("savings", "tabu", "ga")
STOCHASTIC = ("tabu", "ga")


def heuristic_pairwise(rows: list[dict], out_dir: pathlib.Path) -> None:
    """Head-to-head comparison between every pair of heuristics.

    Para cada par (metodo, instancia) toma-se o melhor custo entre as
    sementes e o tempo dessa execucao. Em seguida, para cada par ordenado
    (A, B), contam-se vitorias, derrotas e empates no conjunto comum de
    instancias e calcula-se o gap relativo medio
    gap% = 100*(custo_A - custo_B) / custo_B; positivo indica A pior.
    """
    best: dict[tuple, tuple[float, float]] = {}
    for r in rows:
        if r["method"] not in HEURISTICS or not r["feasible"]:
            continue
        key = (r["method"], r["instance"])
        cur = best.get(key)
        if cur is None or r["cost_total"] < cur[0]:
            best[key] = (r["cost_total"], r["runtime_sec"])

    eps = 1e-6
    with (out_dir / "heuristic_pairwise.csv").open("w", newline="") as f:
        w = csv.writer(f)
        w.writerow(["method_a", "method_b", "n_instances",
                    "a_wins", "b_wins", "ties",
                    "mean_gap_pct", "median_gap_pct",
                    "mean_runtime_ratio_a_over_b"])
        for a in HEURISTICS:
            for b in HEURISTICS:
                if a == b:
                    continue
                inst_a = {i for (m, i) in best if m == a}
                inst_b = {i for (m, i) in best if m == b}
                common = sorted(inst_a & inst_b)
                if not common:
                    continue
                gaps, ratios = [], []
                a_wins = b_wins = ties = 0
                for i in common:
                    ca, ta = best[(a, i)]
                    cb, tb = best[(b, i)]
                    if cb > 0:
                        gaps.append(100.0 * (ca - cb) / cb)
                    if tb > eps:
                        ratios.append(ta / tb)
                    if   ca < cb - eps: a_wins += 1
                    elif ca > cb + eps: b_wins += 1
                    else:               ties   += 1
                w.writerow([
                    a, b, len(common),
                    a_wins, b_wins, ties,
                    f"{statistics.mean(gaps):.4f}"   if gaps   else "",
                    f"{statistics.median(gaps):.4f}" if gaps   else "",
                    f"{statistics.mean(ratios):.4f}" if ratios else "",
                ])


def _expected_best_of_k(sorted_costs: list[float], k: int) -> float:
    """E[min] over a uniformly random size-k subset of K pre-sorted costs.

    Using P(c_(i) is the subset min) = C(K-i, k-1) / C(K, k).
    """
    K = len(sorted_costs)
    if k <= 0 or k > K:
        return float("nan")
    denom = math.comb(K, k)
    total = 0.0
    for i in range(1, K - k + 2):        # i = 1 .. K-k+1
        total += sorted_costs[i - 1] * math.comb(K - i, k - 1)
    return total / denom


def seed_efficiency(rows: list[dict], out_dir: pathlib.Path) -> None:
    """Ganho obtido ao executar as heuristicas com mais sementes.

    Para cada par (metodo, instancia) com ao menos duas execucoes viaveis,
    calcula-se o valor esperado do melhor entre k sementes, para k de 1 ate K,
    normalizado pelo melhor entre todas as K. As curvas sao entao agregadas
    sobre as instancias.
    """
    by_mi: dict[tuple, list[float]] = defaultdict(list)
    for r in rows:
        if r["method"] not in STOCHASTIC or not r["feasible"]:
            continue
        by_mi[(r["method"], r["instance"])].append(r["cost_total"])

    # (metodo, k) -> gap percentual contra o melhor entre K, por instancia
    curves: dict[str, dict[int, list[float]]] = {m: defaultdict(list) for m in STOCHASTIC}
    raw:    dict[str, dict[int, list[float]]] = {m: defaultdict(list) for m in STOCHASTIC}

    for (method, _inst), costs in by_mi.items():
        if len(costs) < 2:
            continue
        sc = sorted(costs)
        K = len(sc)
        best_of_K = sc[0]                # deterministic min over all seeds
        for k in range(1, K + 1):
            E = _expected_best_of_k(sc, k)
            if best_of_K > 0:
                curves[method][k].append(100.0 * (E - best_of_K) / best_of_K)
            raw[method][k].append(E)

    with (out_dir / "seed_efficiency.csv").open("w", newline="") as f:
        w = csv.writer(f)
        w.writerow(["method", "k_seeds", "instances",
                    "mean_gap_vs_best_pct", "median_gap_vs_best_pct",
                    "mean_expected_cost"])
        for method in STOCHASTIC:
            for k in sorted(curves[method]):
                gaps = curves[method][k]
                raws = raw[method][k]
                if not gaps:
                    continue
                w.writerow([
                    method, k, len(gaps),
                    f"{statistics.mean(gaps):.4f}",
                    f"{statistics.median(gaps):.4f}",
                    f"{statistics.mean(raws):.4f}",
                ])

    if plt is None:
        return
    plt.figure(figsize=(6, 4))
    any_series = False
    for method in STOCHASTIC:
        ks = sorted(curves[method])
        ys = [statistics.mean(curves[method][k]) for k in ks if curves[method][k]]
        if ys:
            any_series = True
            plt.plot(ks[:len(ys)], ys, marker="o", label=method)
    if not any_series:
        plt.close()
        return
    plt.xlabel("número de sementes k")
    plt.ylabel("gap médio vs. melhor de todas as sementes (%)")
    plt.title("Eficiência das sementes — ganho marginal por execução extra")
    plt.grid(True, alpha=0.3)
    plt.legend()
    plt.tight_layout()
    save_fig(out_dir / "seed_efficiency", dpi=150)
    plt.close()


def ga_variants_summary(rows: list[dict], out_dir: pathlib.Path) -> None:
    """Per-variant stats across all GA rows that carry a non-empty `variant`.

    Emits results/ga_variants_summary.csv with mean/std cost and runtime per
    variant (aggregated across every instance and every seed).  A companion
    bar-style PNG (mean ± std) is produced when matplotlib is available.
    """
    # variante -> lista de custo e tempo
    by_v: dict[str, list[tuple[float, float]]] = defaultdict(list)
    for r in rows:
        if r["method"] != "ga" or not r["feasible"] or not r["variant"]:
            continue
        by_v[r["variant"]].append((r["cost_total"], r["runtime_sec"]))

    if not by_v:
        return   # no variants were run; skip silently

    with (out_dir / "ga_variants_summary.csv").open("w", newline="") as f:
        w = csv.writer(f)
        w.writerow(["variant", "runs", "instances_covered",
                    "cost_mean", "cost_std",
                    "runtime_mean", "runtime_std"])
        # Conta instancias distintas por variante, para a coluna de cobertura.
        inst_cov: dict[str, set[str]] = defaultdict(set)
        for r in rows:
            if r["method"] == "ga" and r["feasible"] and r["variant"]:
                inst_cov[r["variant"]].add(r["instance"])
        for v in sorted(by_v):
            costs = [c for c, _ in by_v[v]]
            times = [t for _, t in by_v[v]]
            w.writerow([
                v, len(by_v[v]), len(inst_cov[v]),
                f"{statistics.mean(costs):.4f}",
                f"{statistics.pstdev(costs):.4f}" if len(costs) > 1 else "0",
                f"{statistics.mean(times):.4f}",
                f"{statistics.pstdev(times):.4f}" if len(times) > 1 else "0",
            ])

    if plt is None:
        return
    variants = sorted(by_v)
    means = [statistics.mean([c for c, _ in by_v[v]]) for v in variants]
    stds  = [statistics.pstdev([c for c, _ in by_v[v]]) if len(by_v[v]) > 1 else 0.0
             for v in variants]
    plt.figure(figsize=(max(6, 0.8 * len(variants) + 2), 4))
    plt.bar(variants, means, yerr=stds, capsize=4,
            color="#4daf4a", edgecolor="#333", alpha=0.85)
    plt.ylabel("cost_total (média entre instâncias e sementes)")
    plt.xlabel("variante do AG")
    plt.title("Comparação entre variantes do AG (média ± desvio padrão)")
    plt.xticks(rotation=25, ha="right")
    plt.grid(True, axis="y", alpha=0.3)
    plt.tight_layout()
    save_fig(out_dir / "ga_variants_compare", dpi=150)
    plt.close()


def ga_variants_pairwise(rows: list[dict], out_dir: pathlib.Path) -> None:
    """Head-to-head between every pair of GA variants.

    Compara variantes nos pares (instancia, semente) comuns; a mesma semente e
    the fairest match since both runs start from identical RNG state modulo
    the hyperparameters.  When seed sets don't fully overlap, falls back to
    comparing the best-of-seeds cost per instance.
    """
    # (variante, instancia, semente) -> custo
    by_vis: dict[tuple, float] = {}
    for r in rows:
        if r["method"] != "ga" or not r["feasible"] or not r["variant"]:
            continue
        by_vis[(r["variant"], r["instance"], r["seed"])] = r["cost_total"]

    variants = sorted({k[0] for k in by_vis})
    if len(variants) < 2:
        return

    eps = 1e-6
    with (out_dir / "ga_variants_pairwise.csv").open("w", newline="") as f:
        w = csv.writer(f)
        w.writerow(["variant_a", "variant_b",
                    "n_pairs", "a_wins", "b_wins", "ties",
                    "mean_gap_pct", "median_gap_pct"])
        for a in variants:
            for b in variants:
                if a == b:
                    continue
                a_keys = {(i, s) for (v, i, s) in by_vis if v == a}
                b_keys = {(i, s) for (v, i, s) in by_vis if v == b}
                common = sorted(a_keys & b_keys)
                if not common:
                    continue
                gaps = []
                a_wins = b_wins = ties = 0
                for (i, s) in common:
                    ca = by_vis[(a, i, s)]
                    cb = by_vis[(b, i, s)]
                    if cb > 0:
                        gaps.append(100.0 * (ca - cb) / cb)
                    if   ca < cb - eps: a_wins += 1
                    elif ca > cb + eps: b_wins += 1
                    else:               ties   += 1
                w.writerow([
                    a, b, len(common),
                    a_wins, b_wins, ties,
                    f"{statistics.mean(gaps):.4f}"   if gaps else "",
                    f"{statistics.median(gaps):.4f}" if gaps else "",
                ])


def _exact_best_by_instance(rows: list[dict]) -> dict[str, dict]:
    """Pick the best exact result per instance.  Carries status/optimal/LB."""
    out: dict[str, dict] = {}
    for r in rows:
        if r["method"] != "exact" or not r["feasible"]:
            continue
        cur = out.get(r["instance"])
        if cur is None or r["cost_total"] < cur["cost"]:
            out[r["instance"]] = {
                "cost": r["cost_total"],
                "optimal": r["optimal"],
                "status": (r.get("status") or "").strip() or
                          ("optimal" if r["optimal"] else "time_limit"),
                "lower_bound": r["lower_bound"],
            }
    return out


def delta_to_exact(rows: list[dict], out_dir: pathlib.Path) -> None:
    """For every non-exact run, record cost - exact_cost on the same instance.

    Sign convention:
      - `exact_status == "optimal"`  ⇒ delta ≥ 0; closer to zero is better.
      - `exact_status == "time_limit"` (or similar, with a feasible incumbent)
        ⇒ delta < 0 means the heuristic *beat* the CBC incumbent; delta > 0
        means the heuristic is worse than CBC's incumbent (but the true
        optimum might still be lower).
      - If exact has no feasible incumbent on the instance, the row is
        skipped (no delta to compute).

    Aggregates across seeds per (method, variant, instance), reporting both
    mean and best-of-seeds deltas, plus the corresponding gap %.
    """
    exact = _exact_best_by_instance(rows)
    if not exact:
        return

    buckets: dict[tuple, list[tuple[float, float]]] = defaultdict(list)
    n_of_instance: dict[str, int] = {}
    for r in rows:
        if r["method"] == "exact" or not r["feasible"]:
            continue
        if r["instance"] not in exact:
            continue
        buckets[(r["method"], r["variant"], r["instance"])].append(
            (r["cost_total"], r["runtime_sec"]))
        n_of_instance.setdefault(r["instance"], r["N"])

    if not buckets:
        return

    with (out_dir / "delta_to_exact.csv").open("w", newline="") as f:
        w = csv.writer(f)
        w.writerow([
            "method", "variant", "instance", "N",
            "exact_status", "exact_optimal", "exact_cost",
            "method_cost_mean", "method_cost_best",
            "delta_mean", "delta_best",
            "gap_mean_pct", "gap_best_pct",
            "runtime_mean",
        ])
        for (method, variant, inst), pairs in sorted(buckets.items()):
            ex = exact[inst]
            costs = [c for c, _ in pairs]
            times = [t for _, t in pairs]
            cm, cb = statistics.mean(costs), min(costs)
            w.writerow([
                method, variant, inst, n_of_instance[inst],
                ex["status"], 1 if ex["optimal"] else 0,
                f"{ex['cost']:.4f}",
                f"{cm:.4f}", f"{cb:.4f}",
                f"{cm - ex['cost']:.4f}", f"{cb - ex['cost']:.4f}",
                f"{100.0 * (cm - ex['cost']) / ex['cost']:.4f}"
                    if ex["cost"] > 0 else "",
                f"{100.0 * (cb - ex['cost']) / ex['cost']:.4f}"
                    if ex["cost"] > 0 else "",
                f"{statistics.mean(times):.4f}",
            ])

    if plt is None:
        return
    by_method: dict[str, list[float]] = defaultdict(list)
    for (method, _variant, inst), pairs in buckets.items():
        ex = exact[inst]
        if ex["cost"] <= 0:
            continue
        best = min(c for c, _ in pairs)
        by_method[method].append(100.0 * (best - ex["cost"]) / ex["cost"])
    if not by_method:
        return
    methods_ordered = sorted(by_method)
    data = [by_method[m] for m in methods_ordered]
    plt.figure(figsize=(max(6, 1.2 * len(methods_ordered) + 2), 4.2))
    plt.boxplot(data, labels=methods_ordered, showmeans=True, whis=1.5)
    plt.axhline(0, color="black", linestyle="--", alpha=0.6,
                label="custo do exato")
    plt.ylabel("gap vs. custo do exato (%)")
    plt.title("Distribuição do gap heurístico contra o exato\n"
              "(positivo = heuristica pior; negativo = heuristica bate o "
              "incumbente do solver)")
    plt.grid(True, axis="y", alpha=0.3)
    plt.legend(loc="best")
    plt.tight_layout()
    save_fig(out_dir / "delta_distribution", dpi=150)
    plt.close()


def feasibility_by_size(rows: list[dict], out_dir: pathlib.Path) -> None:
    """Feasibility rate and runtime split per (method, N).

    Heuristics sometimes return infeasible solutions on tight-fleet instances;
    this table makes the failure rate explicit, and reports runtime separately
    for feasible and infeasible runs.
    """
    by_mn: dict[tuple, list[dict]] = defaultdict(list)
    for r in rows:
        by_mn[(r["method"], r["N"])].append(r)

    with (out_dir / "feasibility_by_size.csv").open("w", newline="") as f:
        w = csv.writer(f)
        w.writerow([
            "method", "N", "n_runs", "n_feasible", "n_infeasible",
            "feasibility_rate_pct",
            "runtime_mean_all", "runtime_mean_feasible",
            "runtime_mean_infeasible",
            "cost_mean_feasible",
        ])
        for (method, N), runs in sorted(by_mn.items()):
            feas   = [r for r in runs if r["feasible"]]
            infeas = [r for r in runs if not r["feasible"]]
            rate = 100.0 * len(feas) / len(runs) if runs else 0.0
            all_t  = [r["runtime_sec"] for r in runs]
            feas_t = [r["runtime_sec"] for r in feas]
            infs_t = [r["runtime_sec"] for r in infeas]
            feas_c = [r["cost_total"] for r in feas]
            def _m(xs): return f"{statistics.mean(xs):.4f}" if xs else ""
            w.writerow([
                method, N, len(runs), len(feas), len(infeas),
                f"{rate:.2f}",
                _m(all_t), _m(feas_t), _m(infs_t), _m(feas_c),
            ])

    if plt is None:
        return
    methods = sorted({r["method"] for r in rows})
    sizes = sorted({r["N"] for r in rows})
    plt.figure(figsize=(6, 4))
    for method in methods:
        xs, ys = [], []
        for N in sizes:
            runs = by_mn.get((method, N), [])
            if not runs:
                continue
            xs.append(N)
            ys.append(100.0 * sum(1 for r in runs if r["feasible"]) / len(runs))
        if xs:
            plt.plot(xs, ys, marker="o", label=method)
    plt.xlabel("N (clientes)")
    plt.ylabel("taxa de viabilidade (%)")
    plt.title("Taxa de viabilidade por método e tamanho da instância")
    plt.ylim(-5, 105)
    plt.grid(True, alpha=0.3)
    plt.legend()
    plt.tight_layout()
    save_fig(out_dir / "feasibility_vs_n", dpi=150)
    plt.close()


def gap_vs_n_plot(rows: list[dict], out_dir: pathlib.Path) -> None:
    """Plot mean gap-to-exact (%) against N, one curve per method."""
    if plt is None:
        return
    exact = _exact_best_by_instance(rows)
    by_mn: dict[tuple, list[float]] = defaultdict(list)
    for r in rows:
        if r["method"] == "exact" or not r["feasible"]:
            continue
        ex = exact.get(r["instance"])
        if ex is None or ex["cost"] <= 0:
            continue
        by_mn[(r["method"], r["N"])].append(
            100.0 * (r["cost_total"] - ex["cost"]) / ex["cost"])

    if not by_mn:
        return
    methods = sorted({k[0] for k in by_mn})
    sizes = sorted({k[1] for k in by_mn})
    plt.figure(figsize=(6, 4))
    for method in methods:
        xs, ys, errs = [], [], []
        for N in sizes:
            vals = by_mn.get((method, N), [])
            if vals:
                xs.append(N)
                ys.append(statistics.mean(vals))
                errs.append(statistics.pstdev(vals) if len(vals) > 1 else 0.0)
        if xs:
            plt.errorbar(xs, ys, yerr=errs, marker="o", capsize=3, label=method)
    plt.axhline(0, color="black", linestyle="--", alpha=0.5)
    plt.xlabel("N (clientes)")
    plt.ylabel("gap medio vs. exato (%)")
    plt.title("Gap heuristico x tamanho da instancia\n"
              "(médio entre sementes, ± desvio padrão)")
    plt.grid(True, alpha=0.3)
    plt.legend()
    plt.tight_layout()
    save_fig(out_dir / "gap_vs_n", dpi=150)
    plt.close()


def ga_variants_by_seed(rows: list[dict], out_dir: pathlib.Path) -> None:
    """Matrix variant x seed -> mean cost plus per-variant line plot."""
    by_vs: dict[tuple, list[float]] = defaultdict(list)
    variants_s: set[str] = set()
    seeds_s: set[int] = set()
    for r in rows:
        if r["method"] != "ga" or not r["feasible"] or not r["variant"]:
            continue
        variants_s.add(r["variant"])
        seeds_s.add(r["seed"])
        by_vs[(r["variant"], r["seed"])].append(r["cost_total"])

    if not variants_s:
        return

    variants = sorted(variants_s)
    seeds = sorted(seeds_s)

    with (out_dir / "ga_variants_by_seed.csv").open("w", newline="") as f:
        w = csv.writer(f)
        w.writerow(["variant", *[f"seed_{s}" for s in seeds]])
        for v in variants:
            row = [v]
            for s in seeds:
                vals = by_vs.get((v, s), [])
                row.append(f"{statistics.mean(vals):.4f}" if vals else "")
            w.writerow(row)

    if plt is None:
        return
    plt.figure(figsize=(max(6, 0.5 * len(seeds) + 3), 4))
    for v in variants:
        xs, ys = [], []
        for i, s in enumerate(seeds, start=1):
            vals = by_vs.get((v, s), [])
            if vals:
                xs.append(i)
                ys.append(statistics.mean(vals))
        if xs:
            plt.plot(xs, ys, marker="o", label=v)
    plt.xlabel("rank da seed (1 = menor valor de seed)")
    plt.ylabel("cost_total medio entre instancias")
    plt.title("Custo do AG por seed, uma linha por variante")
    plt.grid(True, alpha=0.3)
    plt.legend()
    plt.tight_layout()
    save_fig(out_dir / "ga_variants_by_seed", dpi=150)
    plt.close()


def pareto_time_cost(rows: list[dict], out_dir: pathlib.Path) -> None:
    """Scatter of runtime (log-scale) x normalised cost across all runs."""
    if plt is None:
        return
    exact = _exact_best_by_instance(rows)
    if not exact:
        return
    colors = {
        "exact":   "#e41a1c",
        "savings": "#377eb8",
        "tabu":    "#4daf4a",
        "ga":      "#984ea3",
    }
    plt.figure(figsize=(6.5, 4.2))
    methods_seen: set[str] = set()
    for r in rows:
        if not r["feasible"]:
            continue
        ex = exact.get(r["instance"])
        if ex is None or ex["cost"] <= 0:
            continue
        if r["runtime_sec"] <= 0:
            continue   # log-scale can't show zero
        methods_seen.add(r["method"])
        plt.scatter(r["runtime_sec"], r["cost_total"] / ex["cost"],
                    alpha=0.45, s=22,
                    color=colors.get(r["method"], "#555555"))
    for m in sorted(methods_seen):
        plt.scatter([], [], color=colors.get(m, "#555555"),
                    label=m, s=22, alpha=0.8)
    plt.axhline(1.0, color="black", linestyle="--", alpha=0.5,
                label="custo do exato")
    plt.xscale("log")
    plt.xlabel("runtime (s, log)")
    plt.ylabel("custo / custo do exato")
    plt.title("Pareto tempo x qualidade (todas as execucoes viaveis)")
    plt.grid(True, alpha=0.3, which="both")
    plt.legend(loc="best")
    plt.tight_layout()
    save_fig(out_dir / "pareto_time_cost", dpi=150)
    plt.close()


def make_plots(rows: list[dict], out_dir: pathlib.Path) -> None:
    if plt is None:
        print("matplotlib not available, skipping plots", file=sys.stderr)
        return

    methods = sorted({r["method"] for r in rows})
    by_mn = defaultdict(list)
    for r in rows:
        if r["feasible"]:
            by_mn[(r["method"], r["N"])].append(r)

    for metric, label, fname in [
        ("cost_total", "custo total", "cost_vs_n.png"),
        ("runtime_sec", "tempo (s)", "runtime_vs_n.png"),
    ]:
        plt.figure(figsize=(6, 4))
        for m in methods:
            xs, ys, errs = [], [], []
            for N in sorted({r["N"] for r in rows}):
                vals = [r[metric] for r in by_mn.get((m, N), [])]
                if vals:
                    xs.append(N)
                    ys.append(statistics.mean(vals))
                    errs.append(statistics.pstdev(vals) if len(vals) > 1 else 0.0)
            if xs:
                # As barras mostram um desvio padrao entre as sementes.
                plt.errorbar(xs, ys, yerr=errs, marker="o",
                             capsize=3, label=m)
        plt.xlabel("N (número de clientes)")
        plt.ylabel(label)
        plt.title(f"{label} por método (média ± desvio entre sementes)")
        if metric == "runtime_sec":
            plt.yscale("log")
        plt.grid(True, alpha=0.3)
        plt.legend()
        plt.tight_layout()
        save_fig(out_dir / pathlib.Path(fname).stem, dpi=150)
        plt.close()


_USED_RE = re.compile(r"vehicles used\s*=\s*(\d+)\s*/\s*(\d+)")
# A parte da instancia e casada de forma gulosa, entao o metodo reconhecido e
# o ultimo do nome: hfvrp_n015_m03_s01_ga_BIG_seed1234 da
# (hfvrp_n015_m03_s01, ga, _BIG_seed1234).
_SVG_NAME_RE = re.compile(
    r"^(?P<inst>.+)_(?P<method>exact|savings|tabu|ga)(?P<rest>_.*)?$")


def fleet_by_size(rows: list[dict], out_dir: pathlib.Path) -> None:
    """Veiculos utilizados por metodo e tamanho, lidos de volta dos SVGs.

    A quantidade de veiculos utilizados aparece no bloco de resumo de cada
    SVG, mas nao e coluna do results.csv. O dado importa para a analise da
    prioridade: mais rotas implicam rotas mais curtas e, portanto, t_i menor
    para todos os clientes, de modo que beta pressiona no sentido de usar mais
    veiculos. Um metodo cujo numero de rotas decorra apenas da capacidade nao
    responde a essa pressao.

    Only covers instances with N <= VISUAL_MAX_N, since that is where SVGs are
    written.  Cross-battery comparison lives in experiments/fleet_vs_beta.py.
    """
    svg_dir = out_dir / "svg"
    if not svg_dir.is_dir():
        return

    n_of = {r["instance"]: r["N"] for r in rows}
    # (metodo, instancia) -> contagens; instancia -> tamanho da frota
    used: dict[tuple[str, str], list[int]] = defaultdict(list)
    fleet: dict[str, int] = {}
    for f in sorted(svg_dir.glob("*.svg")):
        m = _SVG_NAME_RE.match(f.stem)
        if not m:
            continue
        try:
            hit = _USED_RE.search(f.read_text(errors="ignore"))
        except OSError:
            continue
        if not hit:
            continue
        inst = m.group("inst")
        used[(m.group("method"), inst)].append(int(hit.group(1)))
        fleet[inst] = int(hit.group(2))
    if not used:
        return

    # A media e feita primeiro dentro de cada instancia, para que o AG, com 48
    # execucoes, nao pese mais que o exato, com uma.
    buckets: dict[tuple[str, int], list[tuple[float, int]]] = defaultdict(list)
    for (meth, inst), counts in used.items():
        n = n_of.get(inst)
        if n is None:
            continue
        buckets[(meth, n)].append((statistics.fmean(counts), fleet[inst]))

    out_rows = []
    for (meth, n) in sorted(buckets, key=lambda k: (k[0], k[1])):
        vals = buckets[(meth, n)]
        k_mean = statistics.fmean(v for v, _ in vals)
        m_mean = statistics.fmean(M for _, M in vals)
        util = 100.0 * k_mean / m_mean if m_mean else 0.0
        out_rows.append([meth, n, len(vals), round(k_mean, 4),
                         round(m_mean, 4), round(util, 2)])

    with open(out_dir / "fleet_by_size.csv", "w", newline="") as fh:
        w = csv.writer(fh)
        w.writerow(["method", "N", "instances", "vehicles_used_mean",
                    "fleet_size_mean", "utilisation_pct"])
        w.writerows(out_rows)


def main() -> None:
    root = pathlib.Path(__file__).resolve().parent.parent
    csv_path = pathlib.Path(sys.argv[1]) if len(sys.argv) > 1 else root / "results" / "results.csv"
    if not csv_path.exists():
        sys.exit(f"no results file at {csv_path}")
    # Tabelas e figuras ficam ao lado do CSV que descrevem, de modo que uma
    # varredura com um diretorio por beta mantenha cada analise separada.
    out_dir = csv_path.parent
    out_dir.mkdir(parents=True, exist_ok=True)
    rows = load(csv_path)
    summarize(rows, out_dir)
    heuristic_pairwise(rows, out_dir)
    seed_efficiency(rows, out_dir)
    ga_variants_summary(rows, out_dir)
    ga_variants_pairwise(rows, out_dir)
    ga_variants_by_seed(rows, out_dir)
    delta_to_exact(rows, out_dir)
    feasibility_by_size(rows, out_dir)
    gap_vs_n_plot(rows, out_dir)
    pareto_time_cost(rows, out_dir)
    make_plots(rows, out_dir)
    # Por ultimo e protegido por try: le milhares de SVGs do disco, e uma
    # falha aqui nao deve derrubar a analise inteira da bateria.
    try:
        fleet_by_size(rows, out_dir)
    except Exception as exc:                                # noqa: BLE001
        print(f"warning: fleet_by_size skipped ({exc})", file=sys.stderr)
    print(f"wrote summary.csv, summary_by_size.csv, "
          f"heuristic_pairwise.csv, seed_efficiency.csv, "
          f"ga_variants_summary.csv, ga_variants_pairwise.csv, "
          f"ga_variants_by_seed.csv, delta_to_exact.csv, "
          f"feasibility_by_size.csv, fleet_by_size.csv, and plots to {out_dir}")


if __name__ == "__main__":
    main()
