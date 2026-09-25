#!/usr/bin/env python3
"""Quantidade de veiculos utilizados em funcao do peso da prioridade.

A penalidade de prioridade incide sobre a distancia acumulada, de modo que
dividir os clientes em mais rotas reduz t_i para todos, ao preco de mais um
custo fixo. Espera-se, portanto, que valores maiores de beta levem a solucao a
empregar mais veiculos.

A quantidade de veiculos utilizados aparece no bloco de resumo de cada SVG,
mas nao e coluna do results.csv; este script a le de volta dos SVGs, o que
restringe a analise as instancias que foram desenhadas. Uso:

    python3 experiments/fleet_vs_beta.py results

Grava fleet_vs_beta.csv e fleet_vs_beta_by_instance.csv no primeiro diretorio
informado, mais a figura correspondente quando o matplotlib esta disponivel.
"""
from __future__ import annotations

import csv
import os
import pathlib
import re
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

USED_RE = re.compile(r"vehicles used\s*=\s*(\d+)\s*/\s*(\d+)")
# Greedy instance part, so the method token matched is the last one in the stem:
#   hfvrp_n015_m03_s01_ga_BIG_seed1234 -> (hfvrp_n015_m03_s01, ga, _BIG_seed1234)
NAME_RE = re.compile(r"^(?P<inst>.+)_(?P<method>exact|savings|tabu|ga)(?P<rest>_.*)?$")


def beta_of(battery_dir: pathlib.Path) -> float | None:
    """Read the battery's beta from its results.csv (column 5)."""
    for cand in (battery_dir / "results.csv", battery_dir / "results" / "results.csv"):
        if not cand.is_file():
            continue
        counts: dict[float, int] = defaultdict(int)
        with open(cand, newline="") as fh:
            for row in csv.DictReader(fh):
                try:
                    counts[float(row["beta"])] += 1
                except (TypeError, ValueError, KeyError):
                    pass
        if counts:
            return max(counts, key=lambda b: counts[b])
    return None


def svg_dir_of(battery_dir: pathlib.Path) -> pathlib.Path | None:
    for cand in (battery_dir / "svg", battery_dir / "results" / "svg"):
        if cand.is_dir():
            return cand
    return None


def scan(svg_dir: pathlib.Path) -> dict:
    """(method, instance) -> list of vehicles-used counts, plus fleet size M."""
    used: dict[tuple[str, str], list[int]] = defaultdict(list)
    fleet: dict[str, int] = {}
    for f in sorted(svg_dir.glob("*.svg")):
        m = NAME_RE.match(f.stem)
        if not m:
            continue
        try:
            text = f.read_text(errors="ignore")
        except OSError:
            continue
        hit = USED_RE.search(text)
        if not hit:
            continue
        k, M = int(hit.group(1)), int(hit.group(2))
        inst = m.group("inst")
        used[(m.group("method"), inst)].append(k)
        fleet[inst] = M
    return {"used": used, "fleet": fleet}


def discover(args: list[str]) -> dict[float, dict]:
    dirs: list[pathlib.Path] = []
    for a in args:
        p = pathlib.Path(a)
        if not p.is_dir():
            print(f"  aviso: {a} nao e um diretorio; ignorado")
            continue
        sub = sorted(q for q in p.glob("beta_*") if q.is_dir())
        dirs += sub if sub else [p]

    out: dict[float, dict] = {}
    for d in dirs:
        beta = beta_of(d)
        sdir = svg_dir_of(d)
        if beta is None:
            print(f"  aviso: sem results.csv em {d}; beta desconhecido, ignorado")
            continue
        if sdir is None:
            print(f"  aviso: sem svg/ em {d}; ignorado "
                  f"(was VISUAL_MAX_N=0 for that battery?)")
            continue
        if beta in out:
            print(f"  warning: beta={beta:g} seen twice; ignoring {d}")
            continue
        data = scan(sdir)
        n_svg = sum(len(v) for v in data["used"].values())
        out[beta] = data
        print(f"  loaded beta={beta:<7g} {n_svg:5d} SVGs  "
              f"{len(data['fleet']):3d} instances  {sdir}")
    return out


def write_csv(path: pathlib.Path, header: list[str], rows: list[list]) -> None:
    with open(path, "w", newline="") as fh:
        w = csv.writer(fh)
        w.writerow(header)
        w.writerows(rows)
    print(f"  wrote {path}")


def main() -> None:
    args = sys.argv[1:] or ["results_sweep"]
    print("Loading batteries:")
    data = discover(args)
    if len(data) < 2:
        sys.exit("need at least two batteries with distinct betas to compare")

    betas = sorted(data)
    out_dir = pathlib.Path(args[0])

    # ---- aggregate per (beta, method) -----------------------------------
    # Average over seeds/variants within an instance first, so a method with 48
    # runs per instance does not outweigh `exact` with one.
    print("\n=== Frota utilizada por beta e metodo (media entre instancias) ===")
    print(f"{'beta':>8} " + "".join(f"{m:>10}" for m in METHODS))
    agg_rows = []
    series: dict[str, list[tuple[float, float]]] = defaultdict(list)
    for b in betas:
        line = f"{b:>8g} "
        for meth in METHODS:
            per_inst = [st.mean(v) for (m, _i), v in data[b]["used"].items()
                        if m == meth and v]
            if not per_inst:
                line += f"{'-':>10}"
                continue
            mean = st.mean(per_inst)
            line += f"{mean:>10.2f}"
            agg_rows.append([f"{b:g}", meth, len(per_inst), round(mean, 4)])
            series[meth].append((b, mean))
        print(line)
    write_csv(out_dir / "fleet_vs_beta.csv",
              ["beta", "method", "instances", "vehicles_used_mean"], agg_rows)

    # ---- per-instance detail for the exact method ------------------------
    common = None
    for b in betas:
        s = {i for (m, i) in data[b]["used"] if m == "exact"}
        common = s if common is None else (common & s)
    common = sorted(common or [])

    if not common:
        print("\n  nenhuma instancia tem SVG do exato em todas as baterias; "
              "skipping the per-instance table")
        return

    print(f"\n=== Detalhe por instancia (exact, {len(common)} instancias) ===")
    print(f"{'instancia':<26}{'M':>4}" + "".join(f"{'b='+f'{b:g}':>8}" for b in betas))
    print("-" * (30 + 8 * len(betas)))
    det_rows, up, same, down = [], 0, 0, 0
    for inst in common:
        M = data[betas[0]]["fleet"].get(inst, 0)
        ks = [st.mean(data[b]["used"][("exact", inst)]) for b in betas]
        # Compare the lowest beta against the highest.
        if ks[-1] > ks[0] + 1e-9:
            up += 1
            flag = "  <-- sobe"
        elif ks[-1] < ks[0] - 1e-9:
            down += 1
            flag = "  <-- desce"
        else:
            same += 1
            flag = ""
        print(f"{inst:<26}{M:>4}" + "".join(f"{k:>8.1f}" for k in ks) + flag)
        det_rows.append([inst, M] + [round(k, 4) for k in ks])
    write_csv(out_dir / "fleet_vs_beta_by_instance.csv",
              ["instance", "M"] + [f"beta_{b:g}" for b in betas], det_rows)

    lo, hi = betas[0], betas[-1]
    print(f"\n  de beta={lo:g} para beta={hi:g}: sobe em {up}, igual em {same}, "
          f"desce em {down}")
    if up and not down:
        print("  => a prioridade so aumenta a frota utilizada, nunca reduz "
              "(consistente com rotas mais curtas baixando todo t_i)")

    # ---- figure ----------------------------------------------------------
    try:
        import matplotlib
        matplotlib.use("Agg")
        import matplotlib.pyplot as plt
    except ImportError:
        print("\n  matplotlib indisponivel; figura nao gerada")
        return

    fig, ax = plt.subplots(figsize=(7, 5))
    for meth, pts in sorted(series.items()):
        pts = sorted(pts)
        ax.plot([p[0] for p in pts], [p[1] for p in pts], "o-", label=meth)
    ax.set_xlabel("β")
    ax.set_ylabel("veículos utilizados (média entre instâncias)")
    ax.set_title("Frota utilizada vs peso da prioridade")
    ax.legend()
    ax.grid(alpha=.3)
    fig.tight_layout()
    save_fig(out_dir / "fleet_vs_beta", fig=fig, dpi=150)
    plt.close(fig)
    print(f"  wrote {out_dir / 'fleet_vs_beta.png'}")


if __name__ == "__main__":
    main()
