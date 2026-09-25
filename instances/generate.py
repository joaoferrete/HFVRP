#!/usr/bin/env python3
"""Gera instancias sinteticas de HFVRP com prioridade de entrega.

Cada instancia e um arquivo texto no formato descrito no README. Os valores
padrao produzem uma bateria adequada ao modelo exato ate cerca de 30 clientes
e as heuristicas ate 200. Use --sizes para alterar os tamanhos.
"""

from __future__ import annotations
import argparse
import math
import pathlib
import random

CUSTOM_DIR = pathlib.Path(__file__).resolve().parent / "custom"


def euclid(a: tuple[float, float], b: tuple[float, float]) -> float:
    return math.hypot(a[0] - b[0], a[1] - b[1])


def make_instance(N: int, M: int, seed: int, name: str) -> str:
    rng = random.Random(seed)

    coords = [(50.0, 50.0)]                                      # deposito
    coords += [(rng.uniform(0, 100), rng.uniform(0, 100)) for _ in range(N)]

    demands = [0.0] + [float(rng.randint(1, 10)) for _ in range(N)]

    # Metade dos clientes recebe prioridade zero; a outra metade se divide
    # entre os pesos 1, 2 e 5, o que da prioridade media 1,33.
    priorities = [0.0]
    for _ in range(N):
        priorities.append(float(rng.choice([0, 0, 0, 1, 2, 5])))

    # Frota heterogenea. A capacidade cresce com o indice, e os custos fixo e
    # variavel acompanham, de modo que haja um compromisso real entre veiculos
    # pequenos e grandes. A capacidade total cobre ao menos 1,5 vez a demanda.
    total_demand = sum(demands)
    vehicles = []
    base_cap = max(10.0, total_demand / max(M, 1))
    for k in range(M):
        capacity = base_cap * (0.8 + 0.4 * (k / max(M - 1, 1)))
        fixed_cost = 20.0 + 15.0 * k
        variable_cost = 1.0 + 0.2 * k
        vehicles.append((round(capacity, 2), round(fixed_cost, 2), round(variable_cost, 2)))

    # Se ainda assim a capacidade ficar apertada, amplia o maior veiculo.
    while sum(v[0] for v in vehicles) < total_demand * 1.5:
        c, f, v = vehicles[-1]
        vehicles[-1] = (round(c * 1.2, 2), f, v)

    lines = [
        f"NAME: {name}",
        f"CUSTOMERS: {N}",
        f"VEHICLES: {M}",
        "NODE_COORD_SECTION",
    ]
    for i, (x, y) in enumerate(coords):
        lines.append(f"{i} {x:.3f} {y:.3f}")
    lines.append("DEMAND_SECTION")
    for i, d in enumerate(demands):
        lines.append(f"{i} {d:.3f}")
    lines.append("PRIORITY_SECTION")
    for i, p in enumerate(priorities):
        lines.append(f"{i} {p:.3f}")
    lines.append("VEHICLE_SECTION")
    for k, (cap, fc, vc) in enumerate(vehicles):
        lines.append(f"{k + 1} {cap:.3f} {fc:.3f} {vc:.3f}")
    lines.append("EOF")
    return "\n".join(lines) + "\n"


def main() -> None:
    p = argparse.ArgumentParser()
    p.add_argument("--sizes", type=int, nargs="+",
                   default=[1, 2, 5, 10, 15, 20, 30, 50, 75, 100, 150, 200],)
    p.add_argument("--per-size", type=int, default=5,
                   help="instancias por tamanho")
    p.add_argument("--out", type=pathlib.Path, default=CUSTOM_DIR)
    args = p.parse_args()

    args.out.mkdir(parents=True, exist_ok=True)
    written = 0
    for N in args.sizes:
        # O tamanho da frota acompanha o da instancia.
        M = max(3, N // 5)
        for s in range(1, args.per_size + 1):
            name = f"hfvrp_n{N:03d}_m{M:02d}_s{s:02d}"
            seed = N * 1000 + M * 100 + s
            text = make_instance(N, M, seed, name)
            (args.out / f"{name}.vrp").write_text(text)
            written += 1
    print(f"{written} instancias gravadas em {args.out}")


if __name__ == "__main__":
    main()
