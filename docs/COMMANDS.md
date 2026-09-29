# Command reference

Commands are newline-terminated, case-sensitive records written to
`input_pipe`. Whitespace around field values and inside lists is accepted. Blank
lines and lines beginning with `#` are ignored.

Identifiers must contain 5 to 15 letters, digits, underscores, or hyphens.
`init` and `scheduled` values use simulated time units; `scheduled` must be
strictly greater than `init`.

## Patient and service commands

### Emergency

```text
EMERGENCY <id> init:<n> triage:<1-5> stability:<100-1000> tests:[<tests>] meds:[<medicines>]
```

```text
EMERGENCY PAT001 init:0 triage:1 stability:100 tests:[HEMO] meds:[ANALGESICO_A]
```

### Appointment

```text
APPOINTMENT <id> init:<n> scheduled:<n> doctor:<CARDIO|ORTHO|NEURO> tests:[<tests>]
```

```text
APPOINTMENT PAT002 init:0 scheduled:20 doctor:CARDIO tests:[GLIC]
```

### Surgery

```text
SURGERY <id> init:<n> type:<CARDIO|ORTHO|NEURO> scheduled:<n> urgency:<LOW|MEDIUM|HIGH> tests:[<tests>] meds:[<medicines>]
```

Every surgery requires `PREOP` and at least one medicine.

```text
SURGERY PAT003 init:0 type:ORTHO scheduled:20 urgency:HIGH tests:[PREOP] meds:[ANESTESICO_C]
```

### Direct pharmacy request

```text
PHARMACY_REQUEST <id> init:<n> priority:<NORMAL|HIGH|URGENT> items:[<medicine>:<quantity>]
```

```text
PHARMACY_REQUEST REQ001 init:0 priority:URGENT items:[ANALGESICO_A:10,VITAMINA_N:2]
```

### Direct laboratory request

```text
LAB_REQUEST <id> init:<n> priority:<NORMAL|URGENT> lab:<LAB1|LAB2|BOTH> tests:[<tests>]
```

`LAB1` accepts `HEMO` and `GLIC`; `LAB2` accepts `COLEST`, `RENAL`, and
`HEPAT`. `PREOP` uses both laboratories and is therefore valid with `BOTH`.

```text
LAB_REQUEST LAB001 init:0 priority:NORMAL lab:LAB1 tests:[HEMO,GLIC]
```

## Control commands

Manual restock is immediate and does not use an `init` field:

```text
RESTOCK ANALGESICO_A quantity:50
```

Status can target `ALL`, `TRIAGE`, `SURGERY`, `PHARMACY`, or `LAB`:

```text
STATUS ALL
```

## Supported values

Tests: `HEMO`, `GLIC`, `COLEST`, `RENAL`, `HEPAT`, and `PREOP`. A command can
request at most three tests.

The canonical medicines are defined by the 15 stock entries in
`config/config.txt`. A command can request at most five medicines. The short
aliases `ANALG_A`, `ANTIB_B`, `ANEST_C`, `CARDIOV_F`, `NEUROLOG_G`, and
`ORTOPED_H` are normalized to their canonical names.

## Sending commands

Start the simulator in one terminal:

```bash
make run
```

Send one command from another terminal:

```bash
printf '%s\n' 'STATUS ALL' > input_pipe
```

Or send a checked-in scenario:

```bash
cat tests/sample_commands/commands_basic.txt > input_pipe
```

Use `Ctrl+C` in the simulator terminal for a clean shutdown. `SIGUSR1` prints
statistics to standard output and `SIGUSR2` writes a snapshot below
`results/stats_snapshots/`.
