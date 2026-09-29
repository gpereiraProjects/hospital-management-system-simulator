# Sistema Integrado de Gestão Hospitalar

**Instituição:** Instituto Politécnico de Viseu - Escola Superior de Tecnologia e Gestão de Viseu (ESTGV)
**Curso:** Licenciatura em Engenharia Informática
**Unidade Curricular:** Sistemas Operativos
**Ano Letivo:** 2024/2025

---

## 1. Resumo do Projeto

Este projeto consiste no desenvolvimento de uma simulação de um ambiente hospitalar em ambiente Linux, utilizando a linguagem C. O sistema é composto por múltiplos processos concorrentes que simulam os departamentos de Triagem, Blocos Operatórios, Farmácia e Laboratório, geridos por um processo central.

O objetivo primordial foi a aplicação prática de primitivas avançadas de Sistemas Operativos, com foco na comunicação entre processos (IPC), sincronização, gestão de memória partilhada e prevenção de _race conditions_ e _deadlocks_.

---

## 2. Arquitetura do Sistema

O sistema segue uma arquitetura hierárquica multiprocesso:

1.  **Gestor Central (main):** Processo pai responsável pela inicialização das estruturas IPC, leitura de comandos via _Named Pipe_ (com implementação de leitura por _buffer_ para garantir atomicidade) e criação de _threads_ dedicadas por pedido (`patient_thread`).
2.  **Triagem (triage):** Gere filas de espera com prioridades dinâmicas e encaminha pacientes para os departamentos adequados.
3.  **Blocos Operatórios (surgery):** Gere a ocupação de salas e equipas médicas. Implementa um mecanismo de espera síncrona por recursos externos (análises e medicamentos).
4.  **Laboratório (laboratory):** Processa pedidos de análises clínicas com tempos de execução simulados.
5.  **Farmácia (pharmacy):** Gere pedidos de medicamentos e controlo de _stocks_.

---

## 3. Mecanismos de IPC e Sincronização

A integridade e comunicação do sistema são asseguradas pelos seguintes mecanismos:

- **Message Queues (System V):** Utilizadas para a troca assíncrona de mensagens entre departamentos.
  - `MQ_URGENT`: Canal prioritário para cirurgias e pedidos urgentes.
  - `MQ_NORMAL`: Canal para triagem e consultas de rotina.
  - `MQ_RESP`: Canal de retorno dedicado para confirmação de tarefas (handshake).
- **Shared Memory (System V):** Armazenamento de dados partilhados de acesso rápido.
  - Estatísticas Globais.
  - Estado das Salas de Cirurgia.
  - Inventário da Farmácia e Filas de Laboratório.
- **Semáforos (POSIX Named Semaphores):** Controlo de concorrência e acesso a recursos finitos (Salas, Equipas Médicas, Slots de Laboratório).
- **Named Pipe (FIFO):** Interface de entrada de comandos externos (`input_pipe`).
- **Sinais (Unix Signals):**
  - `SIGINT`: Desencadeia o encerramento gracioso (_Graceful Shutdown_), libertando todos os recursos.
  - `SIGUSR1`: Apresenta estatísticas em tempo real na consola (`STDOUT`).
  - `SIGUSR2`: Gera um _snapshot_ das estatísticas para ficheiro.

---

## 4. Estruturas de Dados e Implementação

Destacam-se as seguintes decisões de implementação:

- **Thread-per-Request:** O processo principal delega o processamento de cada comando lido do pipe para uma _thread_ separada (`patient_thread`), evitando o bloqueio do ciclo de leitura.
- **Buffered Reader:** Implementação de um mecanismo de leitura acumulada no _pipe_ para processar corretamente comandos fragmentados pelo SO sob alta carga.
- **Protocolo de Prioridades:** O sistema de mensagens garante que o tipo da mensagem (`mtype`) corresponde à prioridade esperada pelo processo destinatário, prevenindo bloqueios na receção.

---

## 5. Instruções de Compilação e Execução

O projeto inclui um `Makefile` para automatização das tarefas.

**Compilar o projeto:**

```bash
make
```

_Nota: Este comando cria as diretorias necessárias (bin, logs, results) e compila o código com as flags -Wall -Wextra -Werror._

**Limpar ficheiros compilados e logs:**

```bash
make clean
```

**Remoção forçada de recursos IPC (em caso de falha):**

```bash
make ipc_clean
```

**Executar o sistema:**

```bash
./bin/hospital_system
```

---

## 6. Interface de Comandos

O sistema aceita comandos através do ficheiro `input_pipe`. Exemplos de sintaxe suportada:

| Ação                 | Formato do Comando                                              |
| :------------------- | :-------------------------------------------------------------- |
| **Admissão Urgente** | `EMERGENCY [NOME] init: 0 triage: [1-5] stability: [0-100] ...` |
| **Consulta**         | `APPOINTMENT [NOME] init: 0 scheduled: [TEMPO] ...`             |
| **Cirurgia**         | `SURGERY [NOME] ... type: [CARDIO/ORTHO/NEURO] ...`             |
| **Pedido Farmácia**  | `PHARMACY_REQUEST [ID] ... items: [MEDICAMENTO:QTD]`            |
| **Estado Sistema**   | `STATUS` ou `STATUS ALL`                                        |

Para facilitar os testes, estão disponíveis ficheiros de exemplo na pasta `sample_commands/`. Exemplo de utilização:

```bash
cat sample_commands/commands_basic.txt > input_pipe
```

---

## 7. Validação e Testes

O sistema foi submetido a testes rigorosos para garantir a robustez e a ausência de fugas de memória.

### Resultados dos Testes Automatizados

- **Teste Básico:** Validação funcional de todos os fluxos (Triagem -> Cirurgia -> Lab/Farmácia). Resultado: **Sucesso**.
- **Teste de Concorrência:** Processamento de 25 pedidos simultâneos sem perda de mensagens. Resultado: **Sucesso**.
- **Teste de Stress:** Execução contínua durante 30 segundos com carga aleatória.
  - Total de operações processadas: **> 280**
  - Estabilidade: **Sem falhas (crashes) ou deadlocks.**

### Análise de Memória (Valgrind)

A verificação com a ferramenta Valgrind confirmou a ausência de fugas de memória (_memory leaks_) no código desenvolvido.

> _All heap blocks were freed -- no leaks are possible_

---

## 8. Organização de Ficheiros

- `src/`: Código fonte (.c) do gestor central e módulos.
- `include/`: Ficheiros de cabeçalho (.h) com estruturas e definições.
- `config/`: Ficheiro de configuração parametrizável (`config.txt`).
- `bin/`: Executáveis gerados após a compilação.
- `tests/`: Scripts de teste (`.sh`) para validação automática.
- `logs/`: Registos de execução do sistema.
- `results/`: Resultados gerados (relatórios médicos, recibos, estatísticas).

---

## 9. Autor

Projeto desenvolvido e mantido por:

- **Guilherme Lopes Pereira**

---

_Instituto Politécnico de Viseu, 2025_

## 10. Licença

Este projeto é distribuído sob a [Licença MIT](../LICENSE).
