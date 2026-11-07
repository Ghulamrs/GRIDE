# RIDE 5.17 — the manual

Ten pages about the editor, and one about each language it drives. Every page
stands on its own; read them in order the first time and out of order after
that.

**1.0** was the editor for C and C++; **1.1** is the release Shalimar arrived
in; **1.2** let Shalimar borrow, and call C; **3.0** is the release cxx1
arrived in; **3.5** brought the fourth target, tms6747, and **5.1** debugging
on Windows. [Page 1](01-what-it-is.md) has the differences in full.

`Help ▸ Contents` in the editor lists these same pages with a line each, and
`F1` shows the keys. This directory is the long form.

| | |
| --- | --- |
| [1. What it is](01-what-it-is.md) | three languages, three variants, one core |
| [2. Getting started](02-getting-started.md) | first run, the demo, where things land |
| [3. The screen](03-the-screen.md) | panes, tabs, the status bar, what is where |
| [4. Editing](04-editing.md) | indenting, UTF-8, undo, selection, the clipboard |
| [5. Finding](05-finding.md) | find, find again, replace, and what counts as a match |
| [6. The project](06-the-project.md) | `prime.pro`, groups, and a compiler per group |
| [7. Building](07-building.md) | the file or the project, targets, debug and release |
| [8. Debugging](08-debugging.md) | breakpoints, stepping, variables, the call stack |
| [9. The panel](09-the-panel.md) | Console, Debug, Assembly, and enter on a line |
| [10. Keys and the command line](10-keys.md) | every key, every menu, every flag |

One page for each language, for the things that are true of that language and
not of the others:

| | |
| --- | --- |
| [C](c.md) | c90, four targets, DWARF on two of them and CodeView on Windows |
| [C++](cpp.md) | cpp11, four targets, and the host's compiler by name |
| [Shalimar](shalimar.md) | shalimar, the library a file borrows, the indent dialect, and a program that stops itself |
| [C and Shalimar](mixing-c-and-shalimar.md) | calling a C library from a Shalimar program |

And the language itself, in full:

| | |
| --- | --- |
| [Appendix A](appendix-a-shalimar-language.md) | The Shalimar Language — the specification |

Appendix A is a **verbatim copy** of the document that lives in the Shalimar
app's own repository, carried here because this is a separate repository and a
reader has no `../Shalimar` to follow. That copy is the one that wins;
`tools/check-help.sh` diffs the two and says when they have drifted.

Nothing in here describes a feature that is not built. Where something is a
limit, the page says so and says why.
