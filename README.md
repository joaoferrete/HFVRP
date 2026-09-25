# HFVRP com prioridade de entrega

Implementação e comparação de quatro métodos de solução para o problema de
roteamento de veículos com frota heterogênea e prioridade de entrega: um modelo
de programação linear inteira mista, a heurística construtiva de Clarke–Wright,
uma busca tabu e um algoritmo genético.

Este repositório acompanha o trabalho de conclusão de curso de Ciência da
Computação da Universidade Federal do Espírito Santo, campus São Mateus, e
contém o código, as instâncias e os resultados consolidados que sustentam os
números apresentados na monografia.

## O problema

Dado um depósito, um conjunto de clientes com demanda e prioridade conhecidas, e
uma frota de veículos com capacidades e custos distintos, o objetivo é decidir
quais veículos utilizar e em que ordem atender os clientes, minimizando

```
  custo = Σ F_k·y_k + Σ V_k·D_ij·x_ijk  +  β · Σ P_i·t_i
          └──────── custo operacional ────────┘   └ prioridade ┘
```

em que `F_k` e `V_k` são os custos fixo e variável do veículo `k`, `D_ij` é a
distância entre os nós `i` e `j`, `P_i` é a prioridade do cliente `i` e `t_i` é
a distância acumulada até a chegada nele.

O parâmetro **β** regula a importância relativa entre custo operacional e
cumprimento de prioridade, e é o eixo do estudo. Com `β = 0` o modelo recupera
o roteamento com frota heterogênea sem prioridade; conforme β cresce, a
penalidade sobre a distância acumulada passa a pesar. O atendimento de todos os
clientes é obrigatório, de modo que a prioridade decide **quando** cada cliente
é atendido, nunca **se** ele é atendido.

## Estrutura do repositório

```
include/            cabeçalhos C++
src/                implementação dos quatro métodos
main.cpp            interface de linha de comando
Makefile            compilação e orquestração dos experimentos

instances/
  generate.py             gerador de instâncias sintéticas
  convert.py              conversor de instâncias da literatura
  download_benchmarks.sh  baixa e converte os bancos públicos
  tests/                  5 instâncias de validação
  custom/                 60 instâncias sintéticas
  benchmarks/             11 instâncias da literatura

experiments/
  run_benchmarks.sh   executa uma bateria completa para um valor de β
  run_beta_sweep.sh   varre os oito valores de β em sequência
  analyze.py          tabelas e figuras de uma bateria
  compare_betas.py    comparação cruzada entre baterias
  fleet_vs_beta.py    veículos utilizados em função de β
  merge_batteries.py  mescla baterias que rodaram métodos diferentes

results/            bateria final do trabalho, em CSV
output/             destino das execuções novas (não versionado)
```

A pasta `results/` é a bateria definitiva usada na monografia e não deve ser
sobrescrita. Toda execução nova grava em `output/`, que está no `.gitignore`.

## Requisitos

- Compilador C++17 (`g++` 9 ou superior)
- `make`, `python3` (3.10 ou superior)
- Um resolvedor de programação inteira, à escolha:
  - **CBC**, de código aberto, usado por padrão
  - **CPLEX** 20.1, opcional

Para as figuras e tabelas: `matplotlib` e `numpy`. Os scripts de geração e
conversão de instâncias não têm dependências externas.

Instalação do CBC no Ubuntu ou Debian:

```bash
sudo apt install -y coinor-libcbc-dev coinor-libcgl-dev coinor-libclp-dev \
                    coinor-libosi-dev coinor-libcoinutils-dev
pip install matplotlib numpy
```

Para verificar se o ambiente está completo:

```bash
make deps-check
```

## Compilação

```bash
make                    # compila com CBC
make SOLVER=cplex       # compila com CPLEX
```

O binário `hfvrp` é gerado na raiz. Ao trocar de resolvedor, rode `make clean`
antes: os objetos do modelo exato não são separados por backend. Se o CPLEX
estiver instalado fora do caminho padrão, informe o prefixo:

```bash
make SOLVER=cplex CPLEX_ROOT=/caminho/para/CPLEX_Studio201
```

## Uso rápido

Resolver uma instância com cada método:

```bash
./hfvrp --method savings --instance instances/custom/hfvrp_n010_m03_s01.vrp --beta 1
./hfvrp --method tabu    --instance instances/custom/hfvrp_n010_m03_s01.vrp --beta 1
./hfvrp --method ga      --instance instances/custom/hfvrp_n010_m03_s01.vrp --beta 1
./hfvrp --method exact   --instance instances/custom/hfvrp_n010_m03_s01.vrp --beta 1 --time-limit 300
```

Pelo Makefile, com as variáveis expostas na linha de comando:

```bash
make run-tabu INSTANCE=instances/custom/hfvrp_n030_m06_s01.vrp BETA=5 VERBOSE=1
make compare  INSTANCE=instances/custom/hfvrp_n010_m03_s01.vrp BETA=1
make help     # lista todos os alvos e variáveis
```

`./hfvrp` sem argumentos imprime todas as opções. As principais:

| Opção | Efeito |
|---|---|
| `--method` | `exact`, `savings`, `tabu` ou `ga` |
| `--beta F` | peso da prioridade |
| `--time-limit S` | limite de tempo por execução |
| `--seed N` | semente; `null` sorteia pelo relógio |
| `--output CSV` | acrescenta uma linha ao arquivo indicado |
| `--visual` | desenha as rotas em SVG |
| `--verbose` | imprime as rotas |
| `--variant NOME` | rótulo para distinguir configurações no CSV |

## Formato das instâncias

O formato estende o TSPLIB com duas seções próprias, descritas em
[`instances/README.md`](instances/README.md): `PRIORITY_SECTION`, com a
prioridade de cada nó, e `VEHICLE_SECTION`, com capacidade, custo fixo e custo
variável de cada veículo.

## Banco de instâncias

As 76 instâncias já versionadas se dividem em três grupos:

| Grupo | Quantidade | N | Propósito |
|---|---|---|---|
| Validação | 5 | 1 a 3 | verificar corretude contra resposta conhecida |
| Sintéticas | 60 | 1 a 200 | varrer tamanhos de forma controlada |
| Literatura | 11 | 31 a 119 | confrontar com instâncias de referência |

Duas das instâncias de validação são propositalmente inviáveis: a demanda total
excede a capacidade da frota. Elas existem para confirmar que os métodos
reconhecem a inviabilidade em vez de devolver uma solução que viola capacidade,
e por isso nenhuma taxa de viabilidade neste projeto chega a 100%.

Para regerar os dois primeiros grupos:

```bash
make generate-instances    # sintéticas, com semente fixa
make download-benchmarks   # baixa da CVRPLIB e do PyVRP, converte e adiciona prioridade
```

Ambos são determinísticos: com a mesma semente, produzem exatamente os mesmos
arquivos já versionados.

## Reproduzindo os experimentos

O experimento central é uma varredura de oito valores de β sobre as 76
instâncias, com as quatro configurações do algoritmo genético e doze sementes
por meta-heurística. São 4.712 execuções por bateria e 37.696 no total.

### Aviso sobre o custo

A varredura completa, como executada no trabalho, levou **13 dias e 20 horas**
de tempo de parede em um servidor Intel Xeon Silver 4114 com 10 núcleos e
160 GB de RAM, usando seis execuções em paralelo e uma thread por resolvedor.
O modelo exato respondeu por 97% desse custo. Não rode a varredura completa em
uma máquina de trabalho sem antes dimensionar o tempo.

### Ensaio rápido

Antes de comprometer dias de processamento, vale reproduzir o fluxo inteiro em
alguns minutos, restringindo o tamanho das instâncias e o tempo por execução:

```bash
INSTANCE_MAX_N=15 TIME_LIMIT=30 BETAS="0 1" experiments/run_beta_sweep.sh
```

Isso exercita todas as etapas — compilação, bateria, análise — sobre um
subconjunto pequeno, e permite conferir que o ambiente está correto.

### A varredura completa

```bash
nohup experiments/run_beta_sweep.sh > sweep.log 2>&1 &
```

Os parâmetros usados no trabalho, todos sobrescrevíveis pelo ambiente:

```bash
BETAS="0 0.001 0.1 0.25 0.5 1 2 5"
SOLVER=cplex
TIME_LIMIT=18000      # 5 horas por execução
MEM_LIMIT=8192        # MB por processo
THREADS=1             # threads do resolvedor
JOBS=6                # instâncias em paralelo
SEEDS="null 1 10 50 75 100 200 999 1234 9999 19345 null"
GA_CONFIGS="DEFAULT: SMALL:pop=50,mut=0.1,gens=50 MEDIUM:pop=500,mut=0.15,gens=1000 BIG:pop=1000,mut=0.2,gens=1500"
```

A varredura é **retomável**: cada bateria concluída grava um marcador `.done` e
é pulada na invocação seguinte. Se o processo for interrompido por reinício da
máquina ou queda de conexão, basta repetir o mesmo comando. Para refazer um
valor de β, apague o `.done` do diretório correspondente.

Cada bateria grava em `output/sweep/beta_<valor>/`: o `results.csv` com uma
linha por execução, as tabelas derivadas e, para instâncias pequenas, os SVGs
das rotas.

### Reexecutar apenas as heurísticas

Como o modelo exato domina o custo computacional, corrigir uma heurística não
deve exigir pagar novamente pelo MILP. O procedimento é executar apenas as
heurísticas em uma árvore nova e enxertá-las sobre as linhas do exato já
calculadas:

```bash
METHODS="savings tabu ga" SWEEP_ROOT=$PWD/output/heur experiments/run_beta_sweep.sh

python3 experiments/merge_batteries.py output/sweep output/heur -o output/final
```

A mesclagem substitui um método por inteiro, nunca linha a linha, e registra a
procedência de cada um em `merge_provenance.txt`. Foi assim que a bateria em
`results/` foi produzida: o modelo exato vem da varredura completa e as três
heurísticas, de uma reexecução posterior.

### Análise

```bash
# tabelas e figuras de cada bateria
for d in output/final/beta_*; do python3 experiments/analyze.py "$d/results.csv"; done

# comparação cruzada entre os oito β
python3 experiments/compare_betas.py output/final

# veículos utilizados em função de β
python3 experiments/fleet_vs_beta.py output/final
```

## Os scripts

### `experiments/run_benchmarks.sh`

Executa todos os métodos sobre todas as instâncias para **um** valor de β, e
reúne tudo em um CSV. É o alvo do `make benchmark`.

```bash
experiments/run_benchmarks.sh [BETA] [LIMITE_DE_TEMPO]
RESULTS_DIR=output/teste SEEDS="1 2 3" experiments/run_benchmarks.sh 1.0 60
```

Variáveis úteis: `RESULTS_DIR` (destino), `METHODS` (quais métodos rodar),
`SEEDS`, `JOBS` (instâncias em paralelo), `EXACT_MAX_N` (pula o exato acima
desse tamanho), `INSTANCE_MAX_N` (descarta instâncias grandes),
`VISUAL_MAX_N` (até que tamanho gerar SVG) e `GA_CONFIGS`.

### `experiments/run_beta_sweep.sh`

Encadeia uma bateria por valor de β, em sequência, e roda a análise ao fim de
cada uma. É o script usado para produzir os resultados do trabalho, projetado
para ser lançado uma vez e deixado rodando por dias. Retomável, como descrito
acima.

```bash
nohup experiments/run_beta_sweep.sh > sweep.log 2>&1 &
BETAS="0 1" TIME_LIMIT=3600 experiments/run_beta_sweep.sh
```

### `instances/download_benchmarks.sh`

Baixa as instâncias públicas da CVRPLIB (conjunto de Augerat) e do PyVRP,
converte para o formato do projeto e acrescenta prioridades de forma
determinística. Requer `curl`.

```bash
instances/download_benchmarks.sh [SEMENTE]
```

### `experiments/analyze.py`

Resume **uma** bateria: custo e tempo por método e tamanho, confronto direto
entre heurísticas, ganho marginal de sementes adicionais, comparação entre as
variantes do algoritmo genético, taxa de viabilidade e frota utilizada. Grava
as tabelas e as figuras ao lado do CSV analisado.

```bash
python3 experiments/analyze.py output/sweep/beta_1/results.csv
```

### `experiments/compare_betas.py`

Compara baterias **entre si**, que é onde o efeito da prioridade aparece.
Produz a fronteira de Pareto entre custo operacional e atraso ponderado, a
tabela de tratabilidade do modelo exato, a folga da relaxação linear na raiz e
o ranking dos métodos por β.

```bash
python3 experiments/compare_betas.py output/sweep
```

### `experiments/fleet_vs_beta.py`

Mede quantos veículos cada método coloca em operação conforme β cresce. Esse
dado não é coluna do `results.csv`: ele é lido de volta dos SVGs, o que
restringe a análise às instâncias pequenas o bastante para terem sido
desenhadas.

```bash
python3 experiments/fleet_vs_beta.py output/sweep
```

### `experiments/merge_batteries.py`

Mescla baterias que executaram métodos diferentes, permitindo reaproveitar as
execuções caras do modelo exato. Ver a seção anterior.

```bash
python3 experiments/merge_batteries.py BASE SOBREPOSICAO -o DESTINO
```

## Resultados incluídos

`results/` traz a bateria final em CSV, sem os SVGs, que são regeneráveis:

| Arquivo | Conteúdo |
|---|---|
| `beta_<valor>/results.csv` | uma linha por execução, com custos, limites, estado e tempo |
| `beta_<valor>/*.csv` | tabelas derivadas por bateria |
| `beta_tradeoff.csv` | fronteira de Pareto entre custo operacional e atraso |
| `beta_tractability.csv` | ótimos provados, execuções sem solução e CPU por β |
| `root_lp_gap.csv` | folga da relaxação linear na raiz por β |
| `beta_method_ranking.csv` | gap de cada heurística contra o ótimo provado |
| `fleet_vs_beta.csv` | veículos utilizados por método e por β |
| `merge_provenance.txt` | de qual bateria veio cada método |

As colunas do `results.csv` são: `method`, `instance`, `N`, `M`, `beta`,
`seed`, `cost_operational`, `cost_priority`, `cost_total`, `lower_bound`,
`root_lp_bound`, `gap`, `optimal`, `feasible`, `status`, `num_nodes`,
`num_iterations`, `num_solutions`, `runtime_sec` e `variant`.

## Principais resultados

Medidos sobre as 24 instâncias em que o modelo exato provou otimalidade em
todos os oito valores de β, que é a única base sobre a qual a comparação entre
valores de β é legítima:

- **A prioridade tem preço, e ele é crescente.** O joelho da fronteira de
  Pareto está entre β = 0,25 e β = 0,5, e a partir de β ≈ 1 ela satura.
- **β = 0,001 reduz o atraso ponderado em 25,6% sem custo operacional algum.**
  Um peso infinitesimal não altera qual é o ótimo operacional; ele apenas
  seleciona, entre as soluções empatadas, a de menor atraso.
- **A prioridade destrói a tratabilidade do modelo exato.** O maior tamanho
  resolvido até a otimalidade cai de 30 para 15 clientes, e a folga da
  relaxação linear na raiz cresce de 16,3% para 58,5%.
- **O ranking das heurísticas se inverte com β.** O Clarke–Wright degrada por
  um fator de 6,8 e o algoritmo genético por 5,5, enquanto a busca tabu
  permanece a menos de 0,55% do ótimo provado em toda a faixa testada.
- **O mecanismo por trás disso é a frota.** A prioridade pode ser atendida
  reordenando clientes, reagrupando-os entre rotas ou usando mais veículos.
  Apenas o modelo exato e a busca tabu dispõem das três alavancas, e apenas
  eles aumentam a frota utilizada conforme β cresce.

## Licença

MIT. Ver [LICENSE](LICENSE).
