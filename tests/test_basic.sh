#!/bin/bash

# Definições de cores
GREEN='\033[0;32m'
RED='\033[0;31m'
NC='\033[0m'

BINARY="bin/hospital_system"
PIPE="input_pipe"
LOG="logs/hospital_log.txt"

echo -e "${GREEN}=== INICIANDO TESTE BÁSICO ===${NC}"

# 1. Limpeza
rm -f $PIPE $LOG
# Garante que as pastas existem para não dar erro
mkdir -p results/lab_results results/pharmacy_deliveries results/stats_snapshots logs

pkill -f hospital_system

# 2. Arrancar
./$BINARY &
PID=$!
sleep 2

# 3. Enviar Comandos
echo "Enviando comandos..."

echo "EMERGENCY PAC001 init: 0 triage: 1 stability: 100 tests: [HEMO] meds: [ANALGESICO_A]" > $PIPE
sleep 0.5
echo "APPOINTMENT PAC002 init: 5 scheduled: 50 doctor: CARDIO tests: []" > $PIPE
sleep 0.5
echo "SURGERY PAC003 init: 10 type: ORTHO scheduled: 100 urgency: LOW tests: [PREOP] meds: [ANESTESICO_C]" > $PIPE
sleep 0.5
echo "PHARMACY_REQUEST REQ001 init: 5 priority: URGENT items: [ANALGESICO_A:10]" > $PIPE

# 4. Aguardar
echo "Aguardando processamento (30 segundos)..."
sleep 30

# --- ADIÇÃO: PEDIR ESTATÍSTICAS ---
echo "Solicitando Snapshot (SIGUSR2)..."
kill -SIGUSR2 $PID
sleep 1
# ----------------------------------

# 6. Parar
echo "Encerramento (SIGINT)..."
kill -SIGINT $PID
wait $PID

echo -e "${GREEN}Teste concluído. Verifica a pasta results/stats_snapshots/${NC}"