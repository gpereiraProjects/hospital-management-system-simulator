# Contributing

Thank you for taking an interest in the project. Changes should keep the
simulator reproducible on Linux and preserve the observable behavior described
in the requirements traceability document.

## Before opening a change

1. Create a focused branch from `main`.
2. Build the release and debug profiles.
3. Run `make format` after editing C source or headers.
4. Run `make test` and `make test_sanitize`.
5. For synchronization, IPC, or lifecycle changes, also run
   `make check_runtime`.

The complete local publication gate is:

```bash
make ci
```

## Change expectations

- Keep commands and configuration backward compatible unless the change is
  explicitly documented.
- Add a bounded assertion for each behavioral fix or new feature.
- Do not add generated logs, results, binaries, IPC artifacts, or the original
  academic assignment/report.
- Update `docs/REQUIREMENTS_TRACEABILITY.md` when a requirement's evidence or
  status changes.
- Explain any intentional deviation from the assignment in
  `docs/ARCHITECTURE.md`.

## Commit and review scope

Prefer small commits with an imperative summary, such as
`fix: preserve requests across fragmented reads`. A review should be able to
connect each implementation change to a test and, where applicable, a traced
requirement.
