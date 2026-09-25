#!/usr/bin/env python3
"""Mescla baterias de beta que executaram metodos diferentes.

O modelo exato responde por mais de 95% do custo computacional da varredura,
de modo que reexecutar uma heuristica apos uma correcao no codigo nao deve
exigir pagar novamente pelo modelo exato. O procedimento e:

    # 1. reexecuta apenas as heuristicas em uma arvore nova
    METHODS="savings tabu ga" SWEEP_ROOT=$PWD/results_heur \\
        experiments/run_beta_sweep.sh

    # 2. enxerta essas linhas sobre as do exato ja calculadas
    python3 experiments/merge_batteries.py results results_heur -o results_final

    # 3. analisa a arvore mesclada
    for d in results_final/beta_*; do python3 experiments/analyze.py "$d/results.csv"; done
    python3 experiments/compare_betas.py results_final

A ultima fonte prevalece: um metodo presente na base e na sobreposicao e
tomado da sobreposicao, por inteiro, nunca linha a linha. A procedencia de
cada metodo e registrada em merge_provenance.txt.
"""
from __future__ import annotations

import argparse
import csv
import pathlib
import shutil
import sys
from collections import defaultdict

HEADER = ["method", "instance", "N", "M", "beta", "seed", "cost_operational",
          "cost_priority", "cost_total", "lower_bound", "root_lp_bound", "gap",
          "optimal", "feasible", "status", "num_nodes", "num_iterations",
          "num_solutions", "runtime_sec", "variant"]


def battery_dirs(root: pathlib.Path) -> dict[str, pathlib.Path]:
    """beta label -> directory, for each beta_*/results.csv under root."""
    out = {}
    for d in sorted(root.glob("beta_*")):
        if (d / "results.csv").is_file():
            out[d.name] = d
    return out


def read_rows(csv_path: pathlib.Path) -> tuple[list[str], list[dict]]:
    with open(csv_path, newline="") as fh:
        r = csv.DictReader(fh)
        return list(r.fieldnames or []), list(r)


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("base", type=pathlib.Path,
                    help="sweep root with the full battery (provides the methods "
                         "no override supplies)")
    ap.add_argument("override", nargs="+", type=pathlib.Path,
                    help="sweep root(s) whose methods replace the base's; later "
                         "arguments win over earlier ones")
    ap.add_argument("-o", "--out", type=pathlib.Path, required=True,
                    help="output sweep root (created; must not be one of the inputs)")
    ap.add_argument("--copy-svg", action="store_true",
                    help="also copy svg/ from the base, then overlay each "
                         "override's svg/ (off by default: thousands of files)")
    args = ap.parse_args()

    roots = [args.base] + list(args.override)
    for r in roots:
        if not r.is_dir():
            return _die(f"{r} is not a directory")
    if args.out.resolve() in {r.resolve() for r in roots}:
        return _die("--out must differ from the inputs")

    per_root = [battery_dirs(r) for r in roots]
    if not per_root[0]:
        return _die(f"no beta_*/results.csv under {args.base}")

    betas = sorted(per_root[0])
    args.out.mkdir(parents=True, exist_ok=True)
    prov_lines = []

    for beta in betas:
        # method -> (rows, source root) with later roots overwriting earlier
        chosen: dict[str, tuple[list[dict], pathlib.Path]] = {}
        header = HEADER
        for root, mapping in zip(roots, per_root):
            d = mapping.get(beta)
            if d is None:
                continue
            fields, rows = read_rows(d / "results.csv")
            if fields:
                header = fields
            by_method: dict[str, list[dict]] = defaultdict(list)
            for row in rows:
                by_method[row.get("method", "")].append(row)
            for m, rws in by_method.items():
                chosen[m] = (rws, root)

        if not chosen:
            print(f"  {beta}: nada a mesclar; ignorado")
            continue

        out_dir = args.out / beta
        out_dir.mkdir(parents=True, exist_ok=True)
        # Keep the canonical method order so the merged file reads like a
        # normal battery rather than a concatenation.
        order = ["exact", "savings", "tabu", "ga"]
        methods = [m for m in order if m in chosen] + \
                  [m for m in sorted(chosen) if m not in order]

        total = 0
        with open(out_dir / "results.csv", "w", newline="") as fh:
            w = csv.DictWriter(fh, fieldnames=header, extrasaction="ignore")
            w.writeheader()
            for m in methods:
                rows, src = chosen[m]
                w.writerows(rows)
                total += len(rows)
                prov_lines.append(f"{beta}\t{m}\t{len(rows)}\t{src}")

        summary = ", ".join(f"{m}<-{chosen[m][1].name}({len(chosen[m][0])})"
                            for m in methods)
        print(f"  {beta}: {total} rows  [{summary}]")

        if args.copy_svg:
            for root, mapping in zip(roots, per_root):
                d = mapping.get(beta)
                if d and (d / "svg").is_dir():
                    shutil.copytree(d / "svg", out_dir / "svg", dirs_exist_ok=True)

    prov = args.out / "merge_provenance.txt"
    prov.write_text(
        "# Which battery each method's rows came from.\n"
        "# beta\tmethod\trows\tsource\n" + "\n".join(prov_lines) + "\n")
    print(f"\nWrote {args.out} ({len(betas)} batteries)")
    print(f"Provenance in {prov}")
    if not args.copy_svg:
        print("SVGs were not copied (use --copy-svg); fleet_vs_beta.py needs them.")
    return 0


def _die(msg: str) -> int:
    print(f"error: {msg}", file=sys.stderr)
    return 1


if __name__ == "__main__":
    sys.exit(main())
