#!/bin/bash

GREEN='\033[0;32m'
RED='\033[0;31m'
NC='\033[0m'

BINARY="bin/hospital_system"
PIPE="input_pipe"
LOG="logs/hospital_log.txt"

echo -e "${GREEN}=== INICIANDO TESTE DE CONCORRÊNCIA ===${NC}"

# Limpeza e Arranque
rm -f $PIPE $LOG
mkdir -p results/stats_snapshots logs
pkill -f hospital_system

./$BINARY &
PID=$!
sleep 2

# Gerar Carga
echo "A injetar carga elevada..."

for i in {1..20}; do
    PRIO=$(( (i % 5) + 1 ))
    echo "EMERGENCY PAC_STRESS_$i init: 0 triage: $PRIO stability: 800 tests: [] meds: [ANALGESICO_A]" > $PIPE
done

for i in {1..5}; do
    echo "PHARMACY_REQUEST REQ_STRESS_$i init: 0 priority: NORMAL items: [VITAMINA_N:1]" > $PIPE
done

# Aguardar processamento
echo "A processar fila (15 segundos)..."
sleep 15

# --- ADIÇÃO: PEDIR ESTATÍSTICAS ---
echo "Solicitando Snapshot (SIGUSR2)..."
kill -SIGUSR2 $PID
sleep 1
# ----------------------------------

# Encerrar
kill -SIGINT $PID
wait $PID

echo -e "${GREEN}Teste concluído. Verifica a pasta results/stats_snapshots/${NC}"