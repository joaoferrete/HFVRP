#!/usr/bin/env python3
"""Converte instancias da literatura para o formato do projeto.

As colecoes publicas nao trazem prioridade de entrega e nem sempre trazem
frota heterogenea, de modo que o conversor normaliza a entrada e acrescenta as
secoes PRIORITY_SECTION e VEHICLE_SECTION.

Dialetos aceitos:

1. CVRPLIB e TSPLIB CVRP, com CAPACITY unica, NODE_COORD_SECTION e
   DEMAND_SECTION. Como a frota da origem e homogenea, tres tipos de veiculo
   sao sintetizados a partir da capacidade original.

2. PyVRP HFVRP, com CAPACITY_SECTION, VEHICLES_FIXED_COST_SECTION e
   VEHICLES_UNIT_DISTANCE_COST_SECTION. A frota nativa e preservada; apenas o
   numero de copias e limitado.

Em ambos os casos --max-vehicles e um piso, nao um teto: a frota e estendida
ate que a capacidade total cubra a demanda total acrescida de
--capacity-margin. Truncar abaixo disso produziria uma instancia inviavel por
construcao, indistinguivel, nos resultados, de um metodo que falhou.

A prioridade e sorteada de forma reproduzivel: uma fracao --priority-rate dos
clientes recebe P_i em {1, 2, 5}, os demais recebem zero.

Uso:

    python3 instances/convert.py ENTRADA.vrp [opcoes]
    python3 instances/convert.py ENTRADA.vrp --out instances/benchmarks/nome.vrp
"""

from __future__ import annotations

import argparse
import pathlib
import random
import sys


# --------------------------------------------------------------------------
# Parsing helpers
# --------------------------------------------------------------------------

def parse_header(text: str) -> dict:
    """Extract KEY: VALUE header lines into a dict."""
    header = {}
    for line in text.splitlines():
        if ":" not in line:
            continue
        key, _, val = line.partition(":")
        key = key.strip()
        val = val.strip()
        if key and key == key.upper() and " " not in key:
            header[key] = val
    return header


def parse_section(text: str, section: str) -> list[str]:
    """Return the raw lines of a section (everything until the next section
    or EOF marker)."""
    markers = (
        "NODE_COORD_SECTION", "DEMAND_SECTION", "DEPOT_SECTION",
        "CAPACITY_SECTION", "VEHICLES_FIXED_COST_SECTION",
        "VEHICLES_UNIT_DISTANCE_COST_SECTION",
        "PRIORITY_SECTION", "VEHICLE_SECTION",
        "EOF",
    )
    lines = text.splitlines()
    try:
        start = next(i for i, l in enumerate(lines) if l.strip() == section) + 1
    except StopIteration:
        return []
    out = []
    for l in lines[start:]:
        ls = l.strip()
        if not ls:
            continue
        if ls in markers or ":" in ls.split()[0]:
            break
        out.append(ls)
    return out


def load_source(path: pathlib.Path) -> dict:
    """Parse a TSPLIB/CVRPLIB/PyVRP HFVRP file into a neutral dict."""
    text = path.read_text()
    header = parse_header(text)
    type_ = header.get("TYPE", "CVRP").upper()

    coords = {}
    for l in parse_section(text, "NODE_COORD_SECTION"):
        parts = l.split()
        i = int(parts[0])
        coords[i] = (float(parts[1]), float(parts[2]))

    demands = {}
    for l in parse_section(text, "DEMAND_SECTION"):
        parts = l.split()
        demands[int(parts[0])] = float(parts[1])

    depots = []
    for l in parse_section(text, "DEPOT_SECTION"):
        parts = l.split()
        if not parts:
            continue
        v = int(parts[0])
        if v == -1:
            break
        depots.append(v)
    depot_id = depots[0] if depots else 1  # TSPLIB default

    # Fleet
    if type_ in ("HFVRP", "HVRP"):
        caps  = _kv_section(text, "CAPACITY_SECTION")
        fixed = _kv_section(text, "VEHICLES_FIXED_COST_SECTION")
        var   = _kv_section(text, "VEHICLES_UNIT_DISTANCE_COST_SECTION")
        vehicles = [
            (caps[k], fixed.get(k, 0.0), var.get(k, 1.0))
            for k in sorted(caps)
        ]
    else:
        # CVRP: one capacity, used to derive a heterogeneous fleet.
        cap = float(header.get("CAPACITY", 100.0))
        vehicles = None  # generated later
        demands_sum = sum(demands.values())
        header["__derived_capacity__"] = cap
        header["__demands_sum__"] = demands_sum

    return {
        "header":  header,
        "type":    type_,
        "coords":  coords,
        "demands": demands,
        "depot":   depot_id,
        "vehicles": vehicles,
    }


def _kv_section(text: str, section: str) -> dict:
    out = {}
    for l in parse_section(text, section):
        parts = l.split()
        out[int(parts[0])] = float(parts[1])
    return out


# --------------------------------------------------------------------------
# Conversion
# --------------------------------------------------------------------------

def total_demand(source: dict) -> float:
    """Total customer demand, excluding the depot."""
    return sum(q for node, q in source["demands"].items()
               if node != source["depot"])


def synthesise_fleet(source: dict, max_vehicles: int,
                     *, margin: float = 0.10,
                     k_default: int = 3) -> list[tuple[float, float, float]]:
    """Monta, a partir de uma instancia CVRP, uma frota heterogenea de tres
    tipos (pequeno, medio e grande) com ao menos max_vehicles copias.

    Aqui `max_vehicles` e um piso, nao um teto. Uma frota cuja capacidade
    total nao cobre a demanda torna toda solucao inviavel, o que nos resultados
    e indistinguivel de um metodo que falhou. O ciclo prossegue ate que a frota
    cubra a demanda total acrescida de `margin`.
    """
    cap = float(source["header"]["__derived_capacity__"])
    if cap <= 0:
        raise ValueError("source CAPACITY must be positive to synthesise a fleet")
    types = [(0.7 * cap, 0.7 * cap * 10, 0.8),  # small
             (1.0 * cap, 1.0 * cap * 15, 1.0),  # medium
             (1.3 * cap, 1.3 * cap * 20, 1.2)]  # large

    demand = total_demand(source)
    needed = demand * (1.0 + margin)
    fleet: list[tuple[float, float, float]] = []
    carried = 0.0
    # Distribute vehicles roughly evenly across types, and keep cycling past
    # max_vehicles if that many cannot carry the demand.
    while len(fleet) < max_vehicles or carried < needed:
        t = types[len(fleet) % len(types)]
        fleet.append(t)
        carried += t[0]

    if len(fleet) > max_vehicles:
        print(f"  note: fleet extended {max_vehicles} -> {len(fleet)} vehicles; "
              f"{max_vehicles} carried only "
              f"{sum(types[i % len(types)][0] for i in range(max_vehicles)):.0f} "
              f"against demand {demand:.0f} (+{margin:.0%} margin)",
              file=sys.stderr)
    return fleet


def select_fleet(source: dict, max_vehicles: int,
                 *, margin: float = 0.10) -> list[tuple[float, float, float]]:
    """Pick a workable subset of a native HFVRP fleet.

    As instancias FSM declaram uma frota nominalmente ilimitada, com centenas
    de veiculos, quando bastam poucos. Tomar apenas os primeiros `max_vehicles`
    na ordem do arquivo pode deixar a capacidade total muito abaixo da demanda,
    produzindo uma instancia inviavel por construcao. Preserva-se entao os
    primeiros, que trazem a variedade de tipos do arquivo, completando com os
    maiores restantes ate que a frota comporte a carga.
    """
    native = list(source["vehicles"])
    demand = total_demand(source)
    needed = demand * (1.0 + margin)

    fleet = native[:max_vehicles]
    carried = sum(v[0] for v in fleet)
    if carried >= needed:
        return fleet

    short_of = carried
    for v in sorted(native[max_vehicles:], key=lambda v: v[0], reverse=True):
        if carried >= needed:
            break
        fleet.append(v)
        carried += v[0]

    if carried < demand:
        print(f"  aviso: a frota nativa completa, de {len(native)} veiculos, "
              f"carrega apenas {carried:.0f} contra demanda de {demand:.0f}; "
              f"a instancia e inviavel", file=sys.stderr)
    elif carried < needed:
        # Viavel, mas a frota nativa inteira fica abaixo da margem pedida.
        # Vale registrar: uma instancia apertada exercita o tratamento de
        # capacidade das heuristicas mais do que o roteamento.
        print(f"  nota: frota usa os {len(native)} veiculos nativos, com "
              f"{carried:.0f} contra demanda de {demand:.0f} "
              f"({100 * (carried - demand) / demand:.1f}% de folga, abaixo dos "
              f"{margin:.0%} pedidos); viavel, porem apertada", file=sys.stderr)
    else:
        print(f"  nota: frota estendida de {max_vehicles} para {len(fleet)} "
              f"veiculos; {max_vehicles} carregavam apenas {short_of:.0f} contra "
              f"demanda de {demand:.0f} (margem de {margin:.0%})", file=sys.stderr)
    return fleet


def assign_priorities(n_customers: int, rate: float, seed: int) -> list[float]:
    """Return P for customers 1..n (excludes the depot)."""
    rng = random.Random(seed)
    weights = [1, 2, 5]
    out = [0.0] * (n_customers + 1)  # index 0 = depot
    for i in range(1, n_customers + 1):
        if rng.random() < rate:
            out[i] = float(rng.choice(weights))
    return out


def write_project_format(source: dict, out_path: pathlib.Path,
                         name: str,
                         priorities: list[float],
                         vehicles: list[tuple[float, float, float]]) -> None:
    coords = source["coords"]
    demands = source["demands"]
    depot_id = source["depot"]

    # Remapeia os indices TSPLIB para o esquema do projeto.
    ordered_ids = sorted(coords.keys())
    assert depot_id in ordered_ids, f"depot {depot_id} not in NODE_COORD_SECTION"
    remap = {depot_id: 0}
    next_id = 1
    for i in ordered_ids:
        if i != depot_id:
            remap[i] = next_id
            next_id += 1
    n_customers = next_id - 1

    lines = [
        f"NAME: {name}",
        f"CUSTOMERS: {n_customers}",
        f"VEHICLES: {len(vehicles)}",
        "NODE_COORD_SECTION",
    ]
    for src_id, (x, y) in sorted(coords.items(), key=lambda kv: remap[kv[0]]):
        lines.append(f"{remap[src_id]} {x:.6f} {y:.6f}")

    lines.append("DEMAND_SECTION")
    for src_id in sorted(coords.keys(), key=lambda i: remap[i]):
        d = 0.0 if remap[src_id] == 0 else demands.get(src_id, 0.0)
        lines.append(f"{remap[src_id]} {d:.6f}")

    lines.append("PRIORITY_SECTION")
    for i in range(0, n_customers + 1):
        lines.append(f"{i} {priorities[i]:.3f}")

    lines.append("VEHICLE_SECTION")
    for k, (cap, fc, vc) in enumerate(vehicles, start=1):
        lines.append(f"{k} {cap:.3f} {fc:.3f} {vc:.3f}")

    lines.append("EOF")

    out_path.write_text("\n".join(lines) + "\n")


# --------------------------------------------------------------------------
# CLI
# --------------------------------------------------------------------------

def main(argv: list[str] | None = None) -> int:
    p = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    p.add_argument("input", type=pathlib.Path, help="TSPLIB/CVRPLIB/PyVRP file")
    p.add_argument("--out", type=pathlib.Path, default=None,
                   help="output path (default: instances/benchmarks/<name>.vrp)")
    p.add_argument("--name", default=None,
                   help="override the NAME field (default: stem of input)")
    p.add_argument("--priority-rate", type=float, default=0.25,
                   help="fraction of customers with P>0 (default 0.25)")
    p.add_argument("--priority-seed", type=int, default=42)
    p.add_argument("--max-vehicles", type=int, default=10,
                   help="numero de veiculos a escrever. Tratado como piso, "
                        "nao como teto: a frota e estendida se essa "
                        "quantidade nao comportar a demanda da instancia")
    p.add_argument("--capacity-margin", type=float, default=0.10,
                   help="fleet capacity must exceed total demand by this "
                        "fraction (default 0.10).  Guards against emitting an "
                        "instance that is infeasible by construction")
    p.add_argument("--fleet", choices=("as-is", "synthesise"), default="as-is",
                   help="'as-is' keeps the fleet of HFVRP instances; "
                        "'synthesise' forces a 3-type heterogeneous fleet "
                        "(useful for CVRP input).")
    args = p.parse_args(argv)

    src = load_source(args.input)

    if args.fleet == "synthesise" or src["vehicles"] is None:
        vehicles = synthesise_fleet(src, args.max_vehicles,
                                    margin=args.capacity_margin)
    else:
        vehicles = select_fleet(src, args.max_vehicles,
                                margin=args.capacity_margin)

    # n_customers = total nodes minus depot
    n_customers = len(src["coords"]) - 1
    priorities = assign_priorities(n_customers, args.priority_rate, args.priority_seed)

    name = args.name or args.input.stem
    out_path = args.out or (
        pathlib.Path(__file__).resolve().parent / "benchmarks" / f"{name}.vrp"
    )
    out_path.parent.mkdir(parents=True, exist_ok=True)
    write_project_format(src, out_path, name, priorities, vehicles)
    print(f"wrote {out_path}  (N={n_customers}, M={len(vehicles)}, "
          f"priority-rate={args.priority_rate}, seed={args.priority_seed})")
    return 0


if __name__ == "__main__":
    sys.exit(main())
