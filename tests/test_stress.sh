#!/bin/bash

GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m'

BINARY="${BINARY:-build/release/bin/hospital_system}"
PIPE="input_pipe"
LOG="logs/hospital_log.txt"

echo -e "${GREEN}=== INICIANDO TESTE DE STRESS ===${NC}"

rm -f $PIPE $LOG
mkdir -p results/stats_snapshots logs
./$BINARY &
PID=$!
sleep 2

# Loop infinito de comandos por 30 segundos
END_TIME=$((SECONDS+30))
COUNTER=0

while [ $SECONDS -lt $END_TIME ]; do
    let COUNTER=COUNTER+1
    TYPE=$(( (RANDOM % 3) + 1 ))
    
    case $TYPE in
        1) echo "EMERGENCY STRESS_P$COUNTER init: 0 triage: $(( (RANDOM % 5) + 1 )) stability: 500 tests: [] meds: []" > $PIPE ;;
        2) echo "APPOINTMENT STRESS_A$COUNTER init: 0 scheduled: 100 doctor: CARDIO tests: []" > $PIPE ;;
        3) echo "PHARMACY_REQUEST STRESS_R$COUNTER init: 0 priority: NORMAL items: [ANALGESICO_A:1]" > $PIPE ;;
    esac
    
    sleep 0.1
done

echo "Stress test finalizado. $COUNTER comandos enviados."
echo "A aguardar esvaziamento das filas (10s)..."
sleep 10

# --- ADIÇÃO: PEDIR ESTATÍSTICAS ---
echo "Solicitando Snapshot (SIGUSR2)..."
kill -SIGUSR2 $PID
sleep 1
# ----------------------------------

kill -SIGINT $PID
wait $PID

echo -e "${GREEN}Teste Stress concluído. Verifica a pasta results/stats_snapshots/${NC}"
