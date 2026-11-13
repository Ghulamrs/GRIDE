# The census - what the three outlooks run today

Step 1 of `common\Common_Compilation_Design.md` (sections 8 and 9): before the build planning of Black, Grey and
White is replaced by one block, the exact command lines each of them runs when it builds each example project are
recorded here as ground truth. `gride-plan.exe` will be compared with these files.

| here | what |
|---|---|
| `shim\shim.cpp`, `shim\shim.vcxproj` | the recorder, a small native program that stands in for a tool |
| `census.ps1` | the driver: opens an outlook on a project, builds it, writes the result |
| `baseline\<Outlook>\<project>.<Config>.<target>.txt` | one normalised result per build |
| `baseline\summary.tsv` | one line per build: outlook, project, configuration, target, commands, compile / assemble / link steps, last exit code, seconds |
| `after-step0\` | Black and Grey recorded again (228 builds) after step 0, the one `common\toolchain.json`; same examples, same form |

**What step 0 changed, baseline against `after-step0`, each build compared as a set of commands:** in Grey, the link
command of the 27 Debug builds for Windows that reach a link (the five `grt*.lib` became `grt*d.lib`), and nothing
else; in Black, no command of any build. White was not recorded again: step 0 changed no file of it.

Nothing here is built by `GRIDE3.sln`, and no binary is kept in the repository. Everything else - the copies of the
outlooks, the recorder's program, the working copies of the projects, the raw logs - is under `%TEMP%\gcensus`.

## How it records

None of the outlooks can build from a command line, so each is driven through its window, and the tools tell what
they were asked.

- **The recorder.** In a copy of an outlook's folder each tool `X.exe` (c90, cpp11, shalimar, c2s, masm, link, asm6x,
  lnk6x, sim6747, vm6747) is renamed `X.real.exe` - in the same folder, because the tools find `lib\`, `include\` and
  each other beside their own file - and the recorder is put there as `X.exe`. When the environment variable
  `GRIDE_CENSUS_LOG` names a file, the recorder appends one JSON line to it (a sequence number, the tool, the current
  directory, the command line exactly as `GetCommandLineW` gives it, and INCLUDE, LIB, every `C90_*`, `CPP11_*`,
  `SHALIMAR_*`, `GRIDE_*` variable and C90, CPP11, SHALIMAR, CL, CXX, ASM6X, LNK6X, VM6747, SIM6747, RTS6X, GRT,
  SHMRT6747, SHMRT6747D when set), runs `X.real.exe` with the same command line and standard handles, and appends a
  second line with the exit code and the milliseconds. A tool that a compiler starts itself (White's `cpp11 -c` runs
  masm) is recorded the same way, since the masm beside it is the recorder too.
- **The driver.** For one outlook, project, configuration and target, `census.ps1` makes a fresh copy of the project
  folder, sets the configuration and the target where that outlook keeps them, starts the outlook's `GRIDE.exe` on
  the project, asks for a full build through the Build menu, waits until no tool is running and nothing has been
  recorded for 3 seconds, closes the outlook, and writes the result.

  | | configuration | target | the project is opened as | the build asked for |
  |---|---|---|---|---|
  | Black | `activeConfig` in a `.grws` written beside the project | `activeTarget` in the same file | that `.grws` | Build > Rebuild Workspace |
  | Grey | `activeConfiguration` in the `.pro`; a CCS project: `CcsProjects` in `settings.json` | `arch` in the `.pro` | the `.pro`; a CCS project by its `.project` | Build > Rebuild Project |
  | White | `config` in `%USERPROFILE%\.gride\state.json` | `arch` in the `.pro` | the `.pro`; a CCS project by its folder | Build > Build project (White has no rebuild; every build of it is a full one) |

  The outlook runs on **a desktop of its own** (`CreateDesktop`), so nothing appears on the user's screen and no key
  of the user's reaches it; `-Visible` runs it on the user's desktop instead (the baseline was not made that way,
  and that way has not been tried on a whole build). The menu is opened through UI
  Automation and the item chosen by Down and Enter keys posted to the open menu's window. A box the outlook puts up
  is written into the result and answered No, Cancel or OK, in that order.
- **The settings.** The three share `%APPDATA%\GRIDE\settings.json`, and White keeps `%USERPROFILE%\.gride`.
  `-Prepare` copies both to `%TEMP%\gcensus\backup` once; they are put back from there before every run, so every
  run starts from the settings as they were, and `-RestoreSettings` puts them back at the end.

## Running it

```
powershell -ExecutionPolicy Bypass -File census.ps1 -Prepare
powershell -ExecutionPolicy Bypass -File census.ps1 -Outlook Black -Project c-bank\c-bank.pro -Config Debug -Target x86_64-windows
powershell -ExecutionPolicy Bypass -File census.ps1 -Outlook White -Project ccs\Hello -Config Release -Target tms6747
powershell -ExecutionPolicy Bypass -File census.ps1 -All
powershell -ExecutionPolicy Bypass -File census.ps1 -All -Outlook Grey -Only "cpp-*","c-bank" -Force
powershell -ExecutionPolicy Bypass -File census.ps1 -Renormalise
powershell -ExecutionPolicy Bypass -File census.ps1 -RestoreSettings
```

- `-Prepare` needs the frozen inputs: `%TEMP%\gcensus\base\Black`, `\Grey`, `\White` (each outlook's Release folder:
  `GRIDE.exe`, the tools, `lib\`, `include\`) and `%TEMP%\gcensus\examples` (a copy of `examples\`). It builds the
  recorder with MSBuild (v143, x64, /MT) into `%TEMP%\gcensus\shim`, makes the recording copies under
  `%TEMP%\gcensus\bin`, and backs the settings up.
- `-Project` is relative to `examples\projects`: a `.pro`, or the folder of a CCS project.
- `-All` runs every project in Debug and Release for x86_64-windows and tms6747 - a `.pro` with an `"arch"` only
  for that one, the CCS samples only for tms6747, `mathx-macos.pro` not at all - project by project, the three
  outlooks one after another. A case whose raw result is already there is skipped unless `-Force` is given, so a
  run that was interrupted is carried on by giving the same command again.
- `-Renormalise` writes the `.txt` files and `summary.tsv` again from the raw logs, without building anything.
- Only one outlook runs at a time, and `-RestoreSettings` is the last thing to run.

Under `%TEMP%\gcensus\raw\<Outlook>\`, for each run: `.jsonl` (the recorder's lines), `.meta.json` (the run), `.drive.txt`
(what the driver did), `.output.txt` (the text of the outlook's panes when the build ended), `.build.log` (Black's
own log), `.files.txt` (files the outlook wrote for a tool and named on its command line).

## Reading a result

A header - outlook, project, configuration, target, the number of commands, whether the last link succeeded, how
the build was asked for and how the run ended, any box the outlook put up - then one block per recorded command, in
the order the commands were started:

```
[5] link   link   exit 0
    cwd: <proj>
    cmd: "<bin>\link.exe" /nologo /subsystem:console /out:"<proj>\out\Debug\Win\c-bank.exe" ...
    env: INCLUDE=...
```

`[n]` is the recorder's sequence number, then the tool, what the step is counted as (compile: c90, cpp11, shalimar;
assemble: masm, asm6x; link: link, lnk6x; other), and the exit code. `(started by [n])` marks a command that another
recorded tool started, not the outlook. `GRIDE_CENSUS_LOG` itself is left out of the variables. After the commands
come the files the outlook wrote for a tool and named on a command line (a `ti-link.cmd`), each line after `|`.

Where the outlook gives its own account of the command lines - Black's `.build.log`, Grey's Output pane - the
header's `cross-check` line says whether it names the same command lines as the recorder.

Paths are replaced by tokens, so that runs can be set beside each other:

| token | stands for |
|---|---|
| `<bin>` | the outlook's folder (`GRIDE.exe` and the tools) |
| `<proj>` | the working copy of the project's folder |
| `<temp>` | `%TEMP%` |
| `<home>` | `%USERPROFILE%` |
| `<pid>` | a process id inside a name under `<temp>` (`gride-parts-<pid>`, `c90-<pid>-0.s`) |

A path is recognised with either slash and in any case; the slashes and the case after the token are left as the
outlook wrote them. Other absolute paths (Visual Studio's, the Windows SDK's) are left as they are.

Commands that ran side by side are numbered in the order they began, which may differ from one run to the next;
compare such a result as a set.
