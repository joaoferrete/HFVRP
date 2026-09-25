# HFVRP com frota heterogenea e prioridade de entrega.
#
# Compilacao do binario e orquestracao dos experimentos. O metodo exato aceita
# dois resolvedores, escolhidos por SOLVER:
#
#   make                # CBC (padrao), de codigo aberto
#   make SOLVER=cplex   # IBM ILOG CPLEX 20.1
#
# Ao trocar de resolvedor, rode `make clean` antes: os objetos do modelo exato
# nao sao separados por backend.

CXX      ?= g++
CXXSTD   ?= -std=c++17
OPT      ?= -O2 -DNDEBUG
WARN     ?= -Wall -Wextra -Wpedantic

SOLVER   ?= cbc

ifeq ($(SOLVER),cplex)
CPLEX_ROOT      ?= /opt/ibm/ILOG/CPLEX_Studio201
CPLEX_DIR       ?= $(CPLEX_ROOT)/cplex
CONCERT_DIR     ?= $(CPLEX_ROOT)/concert
CPLEX_ARCH      ?= x86-64_linux
CPLEX_LIBFORMAT ?= static_pic

SOLVER_SRC     := src/exact_model_cplex.cpp
SOLVER_DEFS    := -DHFVRP_USE_CPLEX -DIL_STD
SOLVER_CFLAGS  := -I$(CPLEX_DIR)/include -I$(CONCERT_DIR)/include
SOLVER_LDDIRS  := -L$(CPLEX_DIR)/lib/$(CPLEX_ARCH)/$(CPLEX_LIBFORMAT) \
                  -L$(CONCERT_DIR)/lib/$(CPLEX_ARCH)/$(CPLEX_LIBFORMAT)
SOLVER_LIBS    := -lilocplex -lconcert -lcplex -lm -ldl
else ifeq ($(SOLVER),cbc)
PKG_NAMES  := cbc cgl osi-clp clp coinutils
PKG_CFLAGS := $(shell pkg-config --cflags $(PKG_NAMES) 2>/dev/null)
PKG_LIBS   := $(shell pkg-config --libs $(PKG_NAMES) 2>/dev/null)

ifeq ($(strip $(PKG_CFLAGS)),)
PKG_CFLAGS := -I/usr/include/coin
endif
ifeq ($(strip $(PKG_LIBS)),)
PKG_LIBS := -lCbcSolver -lCbc -lCgl -lOsiClp -lClpSolver -lClp -lOsi -lCoinUtils -lm
endif

SOLVER_SRC    := src/exact_model.cpp
SOLVER_DEFS   := -DHFVRP_USE_CBC
SOLVER_CFLAGS := $(PKG_CFLAGS)
SOLVER_LDDIRS :=
SOLVER_LIBS   := $(PKG_LIBS)
else
$(error SOLVER='$(SOLVER)' desconhecido. Use cbc ou cplex)
endif

CXXFLAGS := $(CXXSTD) $(OPT) $(WARN) -Iinclude $(SOLVER_DEFS) $(SOLVER_CFLAGS)
LDFLAGS  := $(SOLVER_LDDIRS) $(SOLVER_LIBS) -lpthread

SRC := src/instance.cpp src/solution.cpp $(SOLVER_SRC) \
       src/savings.cpp src/tabu.cpp src/genetic.cpp src/visual.cpp \
       main.cpp
OBJ := $(SRC:.cpp=.o)
BIN := hfvrp

# Interpretador com matplotlib e numpy, exigidos pelos scripts de analise.
define _find_py
$(shell for p in python3 python3.12 python3.11 python3.10; do \
    command -v "$$p" >/dev/null 2>&1 || continue; \
    "$$p" -c 'import matplotlib, numpy' >/dev/null 2>&1 && echo "$$p" && break; \
done)
endef
PY ?= $(or $(strip $(_find_py)),python3)

INSTANCE    ?= instances/custom/hfvrp_n010_m03_s01.vrp
BETA        ?= 1.0
SEED        ?= 42
TIME_LIMIT  ?= 60
NODE_LIMIT  ?= 0
THREADS     ?= 0
MEM_LIMIT   ?= 0
VERBOSE     ?= 0
QUIET       ?= 0
VISUAL      ?= 0
OUTPUT      ?= output/run.csv

METHODS     ?= exact savings tabu ga
SEEDS       ?= 1 2 3 4 5
JOBS        ?= 1
RESULTS_DIR ?= output

# Gera SVG automaticamente para instancias ate este N durante a bateria.
VISUAL_MAX_N   ?= 20
# Pula o metodo exato acima deste N (0 nunca pula). Acima de N=30 o modelo
# nao fecha em tempo praticavel e a execucao apenas consome o limite de tempo.
EXACT_MAX_N    ?= 0
# Remove da bateria as instancias acima deste N (0 mantem todas).
INSTANCE_MAX_N ?= 0

# Hiperparametros do AG; vazio mantem os padroes 80/300/0,10/3/0,10.
GA_POP        ?=
GA_GENS       ?=
GA_MUT        ?=
GA_TOURNAMENT ?=
GA_ELITISM    ?=

# Varias configuracoes do AG numa mesma bateria, no formato
# "NOME:chave=valor,chave=valor" separado por espacos. Chaves aceitas:
# pop, gens, mut, tournament, elitism.
GA_CONFIGS    ?=

VFLAG  := $(if $(filter-out 0,$(VERBOSE)),--verbose,)
QFLAG  := $(if $(filter-out 0,$(QUIET)),--quiet,)
OFLAG  := $(if $(strip $(OUTPUT)),--output $(OUTPUT),)
SFLAG  := $(if $(filter-out 0,$(VISUAL)),--visual,)
NLFLAG := $(if $(filter-out 0,$(NODE_LIMIT)),--node-limit $(NODE_LIMIT),)
TFLAG  := $(if $(filter-out 0,$(THREADS)),--threads $(THREADS),)
MFLAG  := $(if $(filter-out 0,$(MEM_LIMIT)),--mem-limit $(MEM_LIMIT),)
EXFLAGS := $(NLFLAG) $(TFLAG) $(MFLAG)

GAFLAGS := \
    $(if $(strip $(GA_POP)),--ga-population $(GA_POP),) \
    $(if $(strip $(GA_GENS)),--ga-generations $(GA_GENS),) \
    $(if $(strip $(GA_MUT)),--ga-mutation $(GA_MUT),) \
    $(if $(strip $(GA_TOURNAMENT)),--ga-tournament $(GA_TOURNAMENT),) \
    $(if $(strip $(GA_ELITISM)),--ga-elitism $(GA_ELITISM),)

.PHONY: all build rebuild clean clean-all deps-check \
        generate-instances download-benchmarks \
        run-exact run-savings run-tabu run-ga compare \
        visualize-one visualize-all benchmark sweep analyze help

all: build

build: $(BIN)

$(BIN): $(OBJ)
	$(CXX) $(CXXFLAGS) -o $@ $^ $(LDFLAGS)

%.o: %.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@

rebuild: clean build

clean:
	rm -f $(OBJ) $(BIN) src/exact_model.o src/exact_model_cplex.o

clean-all: clean
	rm -rf output
	rm -f instances/custom/*.vrp instances/benchmarks/*.vrp

deps-check:
ifeq ($(SOLVER),cplex)
	@root="$(if $(strip $(CPLEX_ROOT)),$(CPLEX_ROOT),/opt/ibm/ILOG/CPLEX_Studio201)"; \
	if [ -f "$$root/cplex/include/ilcplex/ilocplex.h" ]; then \
	    echo "CPLEX encontrado em $$root"; \
	else \
	    echo "CPLEX nao encontrado em $$root"; \
	    echo "Defina CPLEX_ROOT com o prefixo de instalacao do CPLEX Studio."; \
	    exit 1; \
	fi
else
	@pkg-config --exists cbc 2>/dev/null && echo "CBC encontrado ($$(pkg-config --modversion cbc))" \
		|| (echo "CBC nao encontrado. Instale com:" ; \
		    echo "  sudo apt install -y coinor-libcbc-dev coinor-libcgl-dev coinor-libclp-dev coinor-libosi-dev coinor-libcoinutils-dev" ; \
		    exit 1)
endif
	@command -v $(PY) >/dev/null 2>&1 && echo "$(PY) encontrado" || (echo "Python 3 ausente"; exit 1)

generate-instances:
	$(PY) instances/generate.py

download-benchmarks:
	instances/download_benchmarks.sh $(SEED)

run-exact: $(BIN) | results
	./$(BIN) --method exact --instance $(INSTANCE) --beta $(BETA) \
	       --time-limit $(TIME_LIMIT) $(EXFLAGS) --seed $(SEED) $(VFLAG) $(QFLAG) $(OFLAG) $(SFLAG)

run-savings: $(BIN) | results
	./$(BIN) --method savings --instance $(INSTANCE) --beta $(BETA) \
	       --seed $(SEED) $(VFLAG) $(QFLAG) $(OFLAG) $(SFLAG)

run-tabu: $(BIN) | results
	./$(BIN) --method tabu --instance $(INSTANCE) --beta $(BETA) \
	       --time-limit $(TIME_LIMIT) --seed $(SEED) $(VFLAG) $(QFLAG) $(OFLAG) $(SFLAG)

run-ga: $(BIN) | results
	./$(BIN) --method ga --instance $(INSTANCE) --beta $(BETA) \
	       --time-limit $(TIME_LIMIT) --seed $(SEED) $(GAFLAGS) $(VFLAG) $(QFLAG) $(OFLAG) $(SFLAG)

# Roda os quatro metodos na mesma instancia, para inspecao manual.
compare: $(BIN) | results
	@echo "=== $(INSTANCE) (beta=$(BETA)) ==="
	-./$(BIN) --method savings --instance $(INSTANCE) --beta $(BETA) --seed $(SEED) $(OFLAG)
	-./$(BIN) --method tabu    --instance $(INSTANCE) --beta $(BETA) --seed $(SEED) --time-limit $(TIME_LIMIT) $(OFLAG)
	-./$(BIN) --method ga      --instance $(INSTANCE) --beta $(BETA) --seed $(SEED) --time-limit $(TIME_LIMIT) $(GAFLAGS) $(OFLAG)
	-./$(BIN) --method exact   --instance $(INSTANCE) --beta $(BETA) --time-limit $(TIME_LIMIT) $(EXFLAGS) $(OFLAG)

visualize-one: $(BIN) | results
	@set -e; for m in $(METHODS); do \
		echo "-> $(INSTANCE) [$$m]"; \
		ex=""; [ "$$m" = "exact" ] && ex="$(EXFLAGS)"; \
		ga=""; [ "$$m" = "ga"    ] && ga="$(GAFLAGS)"; \
		./$(BIN) --method $$m --instance $(INSTANCE) --beta $(BETA) \
		       --time-limit $(TIME_LIMIT) $$ex $$ga --seed $(SEED) --visual $(OFLAG) || true; \
	done

visualize-all: $(BIN) | results
	@set -e; for f in instances/custom/hfvrp_n005_*.vrp instances/custom/hfvrp_n010_*.vrp instances/custom/hfvrp_n015_*.vrp instances/custom/hfvrp_n020_*.vrp; do \
		[ -e "$$f" ] || continue; \
		for m in $(METHODS); do \
			echo "-> $$(basename $$f) [$$m]"; \
			ex=""; [ "$$m" = "exact" ] && ex="$(EXFLAGS)"; \
			ga=""; [ "$$m" = "ga"    ] && ga="$(GAFLAGS)"; \
			./$(BIN) --method $$m --instance "$$f" --beta $(BETA) \
			       --time-limit $(TIME_LIMIT) $$ex $$ga --seed $(SEED) --visual || true; \
		done; \
	done

benchmark: $(BIN) | results
	SEEDS="$(SEEDS)" NODE_LIMIT="$(NODE_LIMIT)" \
	    THREADS="$(THREADS)" MEM_LIMIT="$(MEM_LIMIT)" JOBS="$(JOBS)" \
	    RESULTS_DIR="$(RESULTS_DIR)" EXACT_MAX_N="$(EXACT_MAX_N)" \
	    VISUAL_MAX_N="$(VISUAL_MAX_N)" INSTANCE_MAX_N="$(INSTANCE_MAX_N)" \
	    METHODS="$(METHODS)" \
	    GA_POP="$(GA_POP)" GA_GENS="$(GA_GENS)" GA_MUT="$(GA_MUT)" \
	    GA_TOURNAMENT="$(GA_TOURNAMENT)" GA_ELITISM="$(GA_ELITISM)" \
	    GA_CONFIGS="$(GA_CONFIGS)" \
	    experiments/run_benchmarks.sh $(BETA) $(TIME_LIMIT)

# Varredura completa de beta: uma bateria por valor, em sequencia.
sweep: $(BIN)
	experiments/run_beta_sweep.sh

analyze:
	$(PY) experiments/analyze.py $(RESULTS_DIR)/results.csv

results:
	@mkdir -p "$(RESULTS_DIR)"

help:
	@echo "HFVRP com prioridade de entrega"
	@echo ""
	@echo "Compilacao"
	@echo "  make build               compila o binario ./hfvrp (SOLVER=cbc|cplex)"
	@echo "  make rebuild             limpa e compila"
	@echo "  make clean               remove objetos e binario"
	@echo "  make clean-all           remove tambem instancias geradas e resultados"
	@echo "  make deps-check          verifica o resolvedor escolhido e o Python"
	@echo ""
	@echo "Instancias"
	@echo "  make generate-instances  gera as instancias sinteticas"
	@echo "  make download-benchmarks baixa e converte as instancias da literatura"
	@echo ""
	@echo "Execucao de um metodo"
	@echo "  make run-exact           modelo exato"
	@echo "  make run-savings         Clarke-Wright"
	@echo "  make run-tabu            busca tabu"
	@echo "  make run-ga              algoritmo genetico"
	@echo "  make compare             os quatro metodos na mesma instancia"
	@echo ""
	@echo "Experimentos"
	@echo "  make benchmark           bateria completa para um valor de beta"
	@echo "  make sweep               varredura dos oito valores de beta"
	@echo "  make analyze             tabelas e figuras a partir do results.csv"
	@echo "  make visualize-one       um SVG por metodo para INSTANCE"
	@echo "  make visualize-all       SVGs para todas as instancias com N <= 20"
	@echo ""
	@echo "Variaveis (sobrescreva na linha de comando)"
	@echo "  INSTANCE=$(INSTANCE)"
	@echo "  BETA=$(BETA)              peso da prioridade; 0 recupera o HFVRP puro"
	@echo "  SEED=$(SEED)               semente das meta-heuristicas"
	@echo "  TIME_LIMIT=$(TIME_LIMIT)        limite por execucao, em segundos"
	@echo "  SOLVER=$(SOLVER)            resolvedor do metodo exato"
	@echo "  THREADS=$(THREADS)             threads do resolvedor (0 usa o padrao)"
	@echo "  MEM_LIMIT=$(MEM_LIMIT)           teto de memoria em MB (apenas CPLEX)"
	@echo "  NODE_LIMIT=$(NODE_LIMIT)          teto de nos de branch-and-bound"
	@echo "  METHODS=\"$(METHODS)\""
	@echo "  SEEDS=\"$(SEEDS)\""
	@echo "  JOBS=$(JOBS)               instancias em paralelo na bateria"
	@echo "  RESULTS_DIR=$(RESULTS_DIR)"
	@echo "  EXACT_MAX_N=$(EXACT_MAX_N)          pula o exato acima deste N"
	@echo "  INSTANCE_MAX_N=$(INSTANCE_MAX_N)       descarta instancias acima deste N"
	@echo "  GA_POP, GA_GENS, GA_MUT, GA_TOURNAMENT, GA_ELITISM"
	@echo "  GA_CONFIGS            varias configuracoes do AG numa bateria"
	@echo ""
	@echo "Exemplo"
	@echo "  make run-tabu INSTANCE=instances/custom/hfvrp_n030_m06_s01.vrp BETA=5 VERBOSE=1"
