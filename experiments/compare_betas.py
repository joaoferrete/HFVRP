#!/usr/bin/env python3
"""Comparacao cruzada entre baterias de beta distintos.

O analyze.py resume uma bateria; este script compara baterias entre si, que e
onde o peso da prioridade aparece. Uso:

    python3 experiments/compare_betas.py results

Um diretorio passado como argumento e varrido em busca de beta_*/results.csv,
no formato produzido pelo run_beta_sweep.sh; na ausencia desse padrao, o
proprio diretorio e tratado como uma bateria. O beta de cada bateria e lido do
CSV, nunca do nome do diretorio.

Grava beta_tradeoff.csv, beta_tractability.csv, root_lp_gap.csv e
beta_method_ranking.csv no primeiro diretorio informado, mais as figuras
correspondentes quando o matplotlib esta disponivel.
"""
from __future__ import annotations

import csv
import os
import pathlib
import statistics as st
import sys
from collections import defaultdict

# Figuras são salvas uma vez por formato: PDF porque a monografia as inclui com
# \includegraphics e um raster amplia mal na impressão, PNG para inspeção rápida.
# Sobrescreva com FIG_FORMATS=pdf (ou png) se quiser apenas um.
FIG_EXTS = tuple(e.strip() for e in
                 (os.environ.get("FIG_FORMATS") or "png,pdf").split(",") if e.strip())


def save_fig(stem, fig=None, **kw):
    target = fig if fig is not None else plt
    for ext in FIG_EXTS:
        target.savefig(f"{stem}.{ext}", **kw)


METHODS = ("exact", "savings", "tabu", "ga")


def fnum(s):
    try:
        return float(s)
    except (TypeError, ValueError):
        return None


def load_battery(csv_path: pathlib.Path) -> tuple[float, list[dict]]:
    """Return (beta, rows).  Rows whose beta differs from the majority are
    descartadas: um arquivo de bateria tem um unico beta, e um valor estranho
    means the file was appended to by a second run."""
    rows = []
    with open(csv_path, newline="") as fh:
        for r in csv.DictReader(fh):
            r["N"] = int(r["N"]) if r["N"] else 0
            r["M"] = int(r["M"]) if r["M"] else 0
            for k in ("beta", "cost_operational", "cost_priority", "cost_total",
                      "lower_bound", "root_lp_bound", "gap", "runtime_sec"):
                r[k] = fnum(r.get(k))
            r["feasible"] = r.get("feasible") == "1"
            r["optimal"] = r.get("optimal") == "1"
            rows.append(r)
    if not rows:
        raise ValueError(f"{csv_path} has no data rows")

    counts = defaultdict(int)
    for r in rows:
        if r["beta"] is not None:
            counts[r["beta"]] += 1
    beta = max(counts, key=lambda b: counts[b])
    kept = [r for r in rows if r["beta"] == beta]
    if len(kept) != len(rows):
        print(f"  warning: {csv_path.name} mixes betas {sorted(counts)}; "
              f"keeping beta={beta:g} ({len(kept)}/{len(rows)} rows)")
    return beta, kept


def discover(args: list[str]) -> dict[float, list[dict]]:
    paths: list[pathlib.Path] = []
    for a in args:
        p = pathlib.Path(a)
        if p.is_file():
            paths.append(p)
            continue
        if not p.is_dir():
            print(f"  aviso: {a} nao e arquivo nem diretorio; ignorado")
            continue
        found = sorted(p.glob("beta_*/results.csv"))
        # Aceita tambem um diretorio de bateria simples, ou com um nivel
        # results/ aninhado.
        found += [q for q in (p / "results.csv", p / "results" / "results.csv")
                  if q.is_file()]
        if not found:
            print(f"  aviso: nenhum results.csv em {a}; ignorado")
        paths += found

    data: dict[float, list[dict]] = {}
    for path in paths:
        try:
            beta, rows = load_battery(path)
        except (ValueError, OSError) as exc:
            print(f"  warning: {path}: {exc}")
            continue
        if beta in data:
            print(f"  warning: beta={beta:g} seen twice; keeping the first "
                  f"({len(data[beta])} rows) and ignoring {path}")
            continue
        data[beta] = rows
        print(f"  loaded beta={beta:<7g} {len(rows):5d} runs  "
              f"{len({r['instance'] for r in rows}):3d} instances  {path}")
    return data


def best_run(rows: list[dict], method: str, instance: str):
    """Cheapest feasible run of `method` on `instance` (None if there is none)."""
    cand = [r for r in rows if r["method"] == method and r["instance"] == instance
            and r["feasible"] and r["cost_total"] is not None]
    return min(cand, key=lambda r: r["cost_total"]) if cand else None


def write_csv(path: pathlib.Path, header: list[str], rows: list[list]) -> None:
    with open(path, "w", newline="") as fh:
        w = csv.writer(fh)
        w.writerow(header)
        w.writerows(rows)
    print(f"  wrote {path}")


# --------------------------------------------------------------------------
# 1. Fronteira de Pareto: custo operacional pago contra atraso evitado
# --------------------------------------------------------------------------
def tradeoff(data, out_dir):
    betas = sorted(data)
    exact = {b: {r["instance"]: r for r in data[b]
                 if r["method"] == "exact" and r["feasible"] and r["optimal"]}
             for b in betas}

    common = None
    for b in betas:
        s = set(exact[b])
        common = s if common is None else (common & s)
    common = sorted(common or [])

    print(f"\n=== 1. Trade-off (exact, proven optimal at every beta: "
          f"{len(common)} instances)")
    if not common:
        print("  nenhuma instancia tem otimo provado em todos os beta; etapa ignorada")
        return [], []

    base = min(betas)
    base_o = st.mean(exact[base][i]["cost_operational"] for i in common)
    base_p = st.mean(exact[base][i]["cost_priority"] for i in common)

    rows = []
    print(f"  {'beta':>8} {'oper':>10} {'d_oper%':>9} {'prio':>10} {'d_prio%':>9} "
          f"{'price':>8}")
    for b in betas:
        o = st.mean(exact[b][i]["cost_operational"] for i in common)
        p = st.mean(exact[b][i]["cost_priority"] for i in common)
        do = 100 * (o - base_o) / base_o if base_o else 0.0
        dp = 100 * (p - base_p) / base_p if base_p else 0.0
        # Custo operacional pago por unidade de atraso removida.
        price = ((o - base_o) / (base_p - p)) if abs(base_p - p) > 1e-9 else ""
        rows.append([f"{b:g}", len(common), round(o, 4), round(do, 4),
                     round(p, 4), round(dp, 4),
                     round(price, 6) if price != "" else ""])
        ps = f"{price:8.4f}" if price != "" else f"{'-':>8}"
        print(f"  {b:>8g} {o:>10.2f} {do:>+8.2f}% {p:>10.2f} {dp:>+8.2f}% {ps}")

    write_csv(out_dir / "beta_tradeoff.csv",
              ["beta", "instances", "cost_operational_mean", "delta_oper_pct",
               "cost_priority_mean", "delta_prio_pct",
               "price_oper_per_delay"], rows)

    # Monotonicidade: o atraso nao deve crescer com beta.
    bad = []
    for inst in common:
        seq = [exact[b][inst]["cost_priority"] for b in betas]
        if any(seq[i + 1] > seq[i] + 1e-6 for i in range(len(seq) - 1)):
            bad.append(inst)
    print(f"  priority not monotone in beta: {len(bad)}/{len(common)} instances"
          + (f" ({', '.join(bad[:5])})" if bad else ""))

    pareto = [(b,
               st.mean(exact[b][i]["cost_priority"] for i in common),
               st.mean(exact[b][i]["cost_operational"] for i in common))
              for b in betas]
    return rows, pareto


# --------------------------------------------------------------------------
# 2. Tratabilidade: o que a prioridade custa ao modelo exato
# --------------------------------------------------------------------------
def tractability(data, out_dir):
    betas = sorted(data)
    print("\n=== 2. MILP tractability vs beta")
    print(f"  {'beta':>8} {'runs':>5} {'optimal':>8} {'no_sol':>7} {'t_limit':>8} "
          f"{'N_max_opt':>10} {'h_total':>9} {'h_wasted':>9}")
    rows = []
    for b in betas:
        ex = [r for r in data[b] if r["method"] == "exact"]
        if not ex:
            continue
        stt = defaultdict(int)
        for r in ex:
            stt[r["status"] or "?"] += 1
        ns = [r["N"] for r in ex if r["optimal"]]
        h_tot = sum(r["runtime_sec"] or 0 for r in ex) / 3600
        # Tempo gasto em execucoes que nao provaram otimalidade.
        h_bad = sum(r["runtime_sec"] or 0 for r in ex if not r["optimal"]) / 3600
        nmax = max(ns) if ns else 0
        rows.append([f"{b:g}", len(ex), stt["optimal"], stt["no_solution"],
                     stt["time_limit"], nmax, round(h_tot, 3), round(h_bad, 3)])
        print(f"  {b:>8g} {len(ex):>5} {stt['optimal']:>8} "
              f"{stt['no_solution']:>7} {stt['time_limit']:>8} "
              f"{nmax:>10} {h_tot:>9.1f} {h_bad:>9.1f}")
    write_csv(out_dir / "beta_tractability.csv",
              ["beta", "exact_runs", "optimal", "no_solution", "time_limit",
               "N_max_optimal", "hours_total", "hours_without_optimum"], rows)
    return rows


# --------------------------------------------------------------------------
# 2b. Por que o modelo fica mais dificil: a relaxacao da raiz afrouxa
# --------------------------------------------------------------------------
def root_lp_gap(data, out_dir):
    """How far the root LP bound sits from the proven optimum, per beta.

    The priority term puts the t_i variables into the objective, so the slack of
    the MTZ big-M constraints stops being merely structural and starts governing
    the objective value itself.  The prediction is that the root bound loosens as
    beta cresce, o que faz o branch-and-bound perder poder de poda;
    and that is measurable, because `root_lp_bound` is a column of results.csv.

    Restricted to runs that proved optimality, since otherwise there is no
    reference to measure the bound against.  Reported both over every such run
    and over the instances that closed at *every* beta, which is the only
    apples-to-apples comparison.
    """
    betas = sorted(data)
    per = {}
    for b in betas:
        per[b] = {}
        for r in data[b]:
            if r["method"] != "exact" or not r["optimal"]:
                continue
            ub, root = r["cost_total"], r["root_lp_bound"]
            if ub is None or root is None or ub <= 0:
                continue
            per[b][r["instance"]] = 100.0 * (ub - root) / ub

    common = None
    for b in betas:
        s = set(per[b])
        common = s if common is None else (common & s)
    common = sorted(common or [])

    print("\n=== 2b. Root LP relaxation gap vs beta  (exact, proven optimal)")
    print("  gap = (optimum - root bound) / optimum")
    print(f"  {'beta':>8} {'runs':>5} {'mean':>8} {'median':>8} {'max':>8} "
          f"{'mean over ' + str(len(common)) + ' common':>24}")
    rows = []
    for b in betas:
        vals = list(per[b].values())
        if not vals:
            continue
        cm = [per[b][i] for i in common] if common else []
        mean_cm = st.mean(cm) if cm else ""
        rows.append([f"{b:g}", len(vals), round(st.mean(vals), 4),
                     round(st.median(vals), 4), round(max(vals), 4),
                     len(cm), round(mean_cm, 4) if cm else ""])
        cm_txt = f"{mean_cm:23.2f}%" if cm else f"{'-':>24}"
        print(f"  {b:>8g} {len(vals):>5} {st.mean(vals):>7.2f}% "
              f"{st.median(vals):>7.2f}% {max(vals):>7.2f}% {cm_txt}")

    write_csv(out_dir / "root_lp_gap.csv",
              ["beta", "runs_all", "gap_mean_all_pct", "gap_median_all_pct",
               "gap_max_all_pct", "instances_common", "gap_mean_common_pct"],
              rows)
    if len(betas) > 1 and common:
        lo, hi = st.mean(per[betas[0]][i] for i in common), \
                 st.mean(per[betas[-1]][i] for i in common)
        if hi > lo:
            print(f"  => the root bound loosens {hi / lo:.1f}x from "
                  f"beta={betas[0]:g} to beta={betas[-1]:g}, which is the "
                  f"mechanism behind the drop in proven optima")
    return [(b, st.mean(per[b][i] for i in common)) for b in betas] if common else []


# --------------------------------------------------------------------------
# 3. Comparacao entre metodos: gap contra o otimo provado, por beta
# --------------------------------------------------------------------------
def ranking(data, out_dir):
    betas = sorted(data)
    print("\n=== 3. Heuristic gap to the proven optimum, per beta")
    print("  (only instances the exact closed at that beta; positive = worse)")
    rows = []
    series = defaultdict(list)   # method -> [(beta, mean_gap)]
    for b in betas:
        ref = {r["instance"]: r for r in data[b]
               if r["method"] == "exact" and r["feasible"] and r["optimal"]
               and r["cost_total"] and r["cost_total"] > 0}
        for meth in ("savings", "tabu", "ga"):
            gaps = []
            for inst, e in ref.items():
                got = best_run(data[b], meth, inst)
                if got is None:
                    continue
                gaps.append(100 * (got["cost_total"] - e["cost_total"])
                            / e["cost_total"])
            if not gaps:
                continue
            rows.append([f"{b:g}", meth, len(gaps), round(st.mean(gaps), 4),
                         round(st.median(gaps), 4), round(max(gaps), 4)])
            series[meth].append((b, st.mean(gaps)))
            print(f"  beta={b:<7g} {meth:<9} n={len(gaps):>3} "
                  f"mean={st.mean(gaps):>7.2f}%  median={st.median(gaps):>7.2f}%  "
                  f"max={max(gaps):>7.2f}%")
    write_csv(out_dir / "beta_method_ranking.csv",
              ["beta", "method", "instances", "gap_mean_pct",
               "gap_median_pct", "gap_max_pct"], rows)

    # A viabilidade deve ser lida junto do gap: um metodo pode parecer bom
    # apenas por ser medido no subconjunto em que produziu solucao.
    print("\n  viabilidade (% de execucoes com solucao viavel)")
    print(f"  {'beta':>8} " + "".join(f"{m:>10}" for m in METHODS))
    for b in betas:
        line = f"  {b:>8g} "
        for m in METHODS:
            rs = [r for r in data[b] if r["method"] == m]
            line += f"{100*sum(r['feasible'] for r in rs)/len(rs):>9.1f}%" if rs \
                else f"{'-':>10}"
        print(line)
    return series


# --------------------------------------------------------------------------
def plots(pareto, series, out_dir, root_series=None):
    try:
        import matplotlib
        matplotlib.use("Agg")
        import matplotlib.pyplot as plt
    except ImportError:
        print("\n  matplotlib indisponivel; figuras nao geradas")
        return

    if pareto:
        fig, ax = plt.subplots(figsize=(7, 5))
        xs = [p[1] for p in pareto]
        ys = [p[2] for p in pareto]
        ax.plot(xs, ys, "o-", color="#3b6ea5")

        # Points where the frontier has saturated sit almost on top of each
        # other, and one label then prints over the other.  Group those betas
        # into a single annotation instead: the collision is information (the
        # frontier stopped moving), so it should read as a statement rather
        # than as a rendering fault.
        span_x = (max(xs) - min(xs)) or 1.0
        span_y = (max(ys) - min(ys)) or 1.0
        groups = [[pareto[0]]]
        for pt in pareto[1:]:
            prev = groups[-1][-1]
            near = (abs(pt[1] - prev[1]) / span_x < 0.02
                    and abs(pt[2] - prev[2]) / span_y < 0.02)
            if near:
                groups[-1].append(pt)
            else:
                groups.append([pt])
        for g in groups:
            # A grouped label ("β=1, 2") on a single dot already reads as
            # "these coincide", and stays language-neutral for translation.
            label = "β=" + ", ".join(f"{b:g}" for b, _, _ in g)
            x, y = g[0][1], g[0][2]
            ax.annotate(label, (x, y), textcoords="offset points",
                        xytext=(8, 5), fontsize=9)
        ax.set_xlabel("atraso ponderado  Σ P$_i$ · t$_i$  (média entre instâncias)")
        ax.set_ylabel("custo operacional (média entre instâncias)")
        ax.set_title("Fronteira de Pareto traçada por β (exato, ótimo provado)")
        ax.grid(alpha=.3)
        fig.tight_layout()
        save_fig(out_dir / "beta_pareto", fig=fig, dpi=150)
        plt.close(fig)
        print(f"  wrote {out_dir / 'beta_pareto.png'}")

    if series:
        fig, ax = plt.subplots(figsize=(7, 5))
        for meth, pts in sorted(series.items()):
            pts = sorted(pts)
            ax.plot([p[0] for p in pts], [p[1] for p in pts], "o-", label=meth)
        ax.set_xlabel("β")
        ax.set_ylabel("gap médio contra o ótimo provado (%)")
        ax.set_title("Qualidade das heurísticas × peso da prioridade")
        ax.legend()
        ax.grid(alpha=.3)
        fig.tight_layout()
        save_fig(out_dir / "beta_ranking", fig=fig, dpi=150)
        plt.close(fig)
        print(f"  wrote {out_dir / 'beta_ranking.png'}")

    if root_series:
        fig, ax = plt.subplots(figsize=(7, 5))
        bs = [b for b, _ in root_series]
        gs = [g for _, g in root_series]
        ax.plot(bs, gs, "o-", color="#b5533b")
        for b, g in root_series:
            ax.annotate(f"{g:.1f}%", (b, g), textcoords="offset points",
                        xytext=(6, -12), fontsize=8)
        # beta spans 0 to 5 with two values below 0.1, so a linear axis buries
        # the low end; symlog keeps beta=0 visible while spreading 0.001-0.1.
        ax.set_xscale("symlog", linthresh=0.001)
        ax.set_xticks(bs)
        ax.set_xticklabels([f"{b:g}" for b in bs])
        ax.set_xlabel("β")
        ax.set_ylabel("folga da relaxação na raiz (%)")
        ax.set_title("Por que o MILP fica mais difícil: a relaxação na raiz afrouxa")
        ax.grid(alpha=.3)
        fig.tight_layout()
        save_fig(out_dir / "beta_root_lp_gap", fig=fig, dpi=150)
        plt.close(fig)
        print(f"  wrote {out_dir / 'beta_root_lp_gap.png'}")


def main() -> None:
    args = sys.argv[1:] or ["results_sweep"]
    print("Loading batteries:")
    data = discover(args)
    if len(data) < 2:
        sys.exit("need at least two batteries with distinct betas to compare")

    first = pathlib.Path(args[0])
    out_dir = first if first.is_dir() else first.parent
    _, pareto = tradeoff(data, out_dir)
    tractability(data, out_dir)
    root_series = root_lp_gap(data, out_dir)
    series = ranking(data, out_dir)
    plots(pareto, series, out_dir, root_series)
    print(f"\nOutputs in {out_dir}")


if __name__ == "__main__":
    main()
