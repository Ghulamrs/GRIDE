# oldnames

The plain spellings Microsoft keeps in `oldnames.lib` for its underscore names - `strdup` for `_strdup` and so on -
for the C6747, where no such library exists (docs\Runtime_Design.md 9.4, the policy of 2026-11-10). One file to a
name, each calling the underscore function. grt's build does not take this folder (`rts6x-c.sources` names none
of it): on Windows the same names are grtcompat's (`toolchain\Windows\RT\compat`).