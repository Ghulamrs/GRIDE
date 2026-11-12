# Reviews

Independent reviews of GRIDE3's tools and runtimes, and one early draft of the design, kept as they were written.
Each is a reading at a commit; later fixes are recorded in the documents they name, not by editing these.

| Document | Date | Subject | Where its findings went |
|---|---|---|---|
| `cpp11 1.25 critical review - Fable 5.1.md` | 2026-10-21 | cpp11 1.25 against ISO/IEC 14882:2011 | cpp11's `Version.h` (1.26 onwards), `tests\conformance` |
| `cpp11 1.30 verification - Fable 5.1.md` | 2026-10-28 | cpp11 1.30, clause by clause | `Version.h` 1.31, `tests\conformance\pos\f130`, `neg\f130` |
| `runtimes critical review - Fable 5.1.md` | 2026-11-05 | grt, RTS6x, printf6x, shmrt as cpp11 builds them | its own section 8; `docs\Runtime_Design.md` section 16 |
| `GRIDE Design and Development - draft 0.5.html` | 2026-10-21 | an early draft of `docs\GRIDE_Design_and_Development.html` | superseded by that document |

The probe programs of the runtimes review lived in a scratch folder that no longer exists; the ones that found
defects are recreated as tests under `toolchain\Windows\RT\tests\review` and `toolchain\TI\RTS6x\tests\review`.
