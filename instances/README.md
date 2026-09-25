# Instâncias

O formato estende o TSPLIB com duas seções próprias, `PRIORITY_SECTION` e
`VEHICLE_SECTION`, necessárias porque as coleções públicas não trazem
prioridade de entrega e nem sempre trazem frota heterogênea.

## Formato

```
NAME: <string>               # identificador, aparece nas saídas
CUSTOMERS: <N>               # número de clientes; o depósito é o nó 0
VEHICLES: <M>                # tamanho da frota

NODE_COORD_SECTION
<id> <x> <y>                 # id 0 é o depósito, 1..N são os clientes

DEMAND_SECTION
<id> <demanda>               # a demanda do depósito é 0

PRIORITY_SECTION
<id> <prioridade>            # P_i, peso do termo beta * P_i * t_i

VEHICLE_SECTION
<id> <capacidade> <custo_fixo> <custo_variavel>

EOF
```

As distâncias são euclidianas, calculadas a partir das coordenadas pelo
carregador. Linhas iniciadas por `#` são ignoradas. O carregador força
`demanda[0] = 0` e `prioridade[0] = 0` na leitura, ainda que o arquivo traga
outros valores.

### Exemplo completo

```
NAME: demo_3c
CUSTOMERS: 3
VEHICLES: 2
NODE_COORD_SECTION
0 0.0 0.0
1 1.0 0.0
2 0.0 1.0
3 1.0 1.0
DEMAND_SECTION
0 0
1 4
2 3
3 5
PRIORITY_SECTION
0 0
1 0
2 5
3 0
VEHICLE_SECTION
1 8  10 1.0
2 10 20 0.8
EOF
```

O veículo 2 é maior e gasta menos por unidade de distância, mas paga mais para
deixar o depósito. O cliente 2 é o único prioritário.

## Pastas

### `tests/` — 5 instâncias de validação

Instâncias de uma a três entregas, com solução conhecida por construção.
Servem para verificar corretude, não desempenho. Duas delas,
`ex2_1v_infeasible` e `ex4_3v_infeasible`, são **propositalmente inviáveis**: a
demanda total excede a capacidade da frota. Existem para confirmar que os
métodos reconhecem a inviabilidade em vez de devolver uma solução que viola
capacidade.

### `custom/` — 60 instâncias sintéticas

Geradas por `generate.py`, com cinco réplicas para cada um de doze tamanhos:

| N | M | N | M |
|---|---|---|---|
| 1 | 3 | 30 | 6 |
| 2 | 3 | 50 | 10 |
| 5 | 3 | 75 | 15 |
| 10 | 3 | 100 | 20 |
| 15 | 3 | 150 | 30 |
| 20 | 4 | 200 | 40 |

Parâmetros do gerador:

| Elemento | Regra |
|---|---|
| Coordenadas | uniformes em `[0, 100]²`, depósito em `(50, 50)` |
| Demanda | inteira uniforme em `[1, 10]` |
| Prioridade | sorteada em `{0, 0, 0, 1, 2, 5}`, ou seja, **metade dos clientes com P > 0** e prioridade média 1,33 |
| Frota | `M = max(3, N/5)`; capacidade cresce com o índice do veículo |
| Custo fixo | `20 + 15·k` |
| Custo variável | `1,0 + 0,2·k` |
| Capacidade total | ao menos 1,5 vez a demanda total |

A semente de cada instância é `N·1000 + M·100 + s`, o que torna a geração
reprodutível.

```bash
python3 instances/generate.py
python3 instances/generate.py --sizes 10 20 --per-size 3
```

### `benchmarks/` — 11 instâncias da literatura

Obtidas por `download_benchmarks.sh`, que baixa os arquivos originais,
converte para o formato acima e acrescenta prioridades.

Seis vêm do conjunto de Augerat, disponível na CVRPLIB. Como são instâncias de
capacidade única, o conversor sintetiza três tipos de veículo multiplicando a
capacidade original por 0,7, 1,0 e 1,3, com custos crescentes com o porte.

Cinco vêm das instâncias de frota heterogênea do PyVRP, cuja frota nativa é
preservada; apenas o número de cópias é limitado.

Em ambos os casos a prioridade é sintética e determinística: 25% dos clientes
recebem `P_i` sorteado em `{1, 2, 5}` e os demais recebem zero, o que dá
prioridade média 0,67. **Essa intensidade difere da usada nas instâncias
sintéticas**, onde metade dos clientes é prioritária, e é um fator de confusão
em comparações diretas entre os dois grupos.

```bash
instances/download_benchmarks.sh [SEMENTE]

python3 instances/convert.py ENTRADA.vrp --out instances/benchmarks/nome.vrp
python3 instances/convert.py ENTRADA.vrp --priority-rate 0.5 --priority-seed 7
```

O conversor trata `--max-vehicles` como piso, e não como teto: a frota é
estendida até que a capacidade total cubra a demanda acrescida de
`--capacity-margin`, evitando emitir instâncias inviáveis por construção.
