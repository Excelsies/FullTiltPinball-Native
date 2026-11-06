# Reverse engineering

How the original executables are analysed, the tools in `re/`, and the measured facts the port is
built on.

## Rules

- Only our own tools, notes and reimplemented source are tracked. The executables, Ghidra's
  exports, decompiled listings and anything quoting game text stay in `re/work/`, which is
  git-ignored.
- The user's Full Tilt folder is read only; work on copies (`re/work/bin/` for the executables,
  `re/work/game/` for the test data).
- Tools print structure, names and numbers. A tool whose output is game data (for example
  `dat_type_attrs.py`) is redirected into `re/work/`.

## Setup

- Python tools: `re/work/.venv` with `capstone` and `pefile`:
  `re/work/.venv/bin/python re/<tool>.py ...`. Tools that need neither run with the system Python.
- Ghidra 12.1.2, installed in `$GHIDRA_INSTALL_DIR` (e.g. `/opt/ghidra`). Headless export of one executable:

      "$GHIDRA_INSTALL_DIR"/support/analyzeHeadless re/work/ghidra-proj fulltilt -import re/work/bin/<EXE> \
          -scriptPath re/ghidra_scripts -postScript ExportAll.java re/work/export

  `ExportAll.java` writes `<EXE>.c` (every function decompiled) and `<EXE>.tsv` (entry, name, size,
  callees). It also creates functions at code pointers found in data, because virtual methods are
  only reachable through vtables. Ghidra refuses a project path that contains a folder starting
  with `.`, so the project stays outside dot folders.

## Workspace (`re/work/`, git-ignored)

| Path | Contents |
|---|---|
| `bin/` | Copies of `CADET.EXE`, `DRAGON.EXE`, `PIRATES.EXE` |
| `export/` | Ghidra exports and the JSON dumps the tools write (control tables, message map, ...) |
| `spec/` | Notes derived from the decompiled code, and inputs some tools and the test runner read (`msgmap.json` for `hookrun.py`'s `ft:` codes, `control_tables_<t>.json` for `rules_tables_gen.py` and `make_rest_watch.py`) |
| `ghidra-proj/` | The Ghidra project |
| `game/` | Test copies of the three table folders |
| `wine-prefix/` | The test harness's Wine prefix |

## Tools (`re/`)

| Tool | Purpose |
|---|---|
| `fn_match.py` | How much of DRAGON.EXE / PIRATES.EXE is the same engine as CADET.EXE (function fingerprints with addresses masked) |
| `fn_diff.py`, `exe_diff3.py` | Pair functions across executables using Ghidra's boundaries and diff the changed ones; three-way comparison |
| `engine_lockstep.py` | Relocation-aware byte comparison of one code region in two executables |
| `disasm.py`, `disasm_cmp.py` | Disassemble a range (code the export lacks); check that other executables hold the same code at a range |
| `field_xrefs.py` | Every instruction that reads or writes `[register + offset]`: the users of a struct field |
| `class_map.py`, `part_vtables.py` | Part type -> constructor -> vtable -> methods, with named vtable slots |
| `control_tables.py`, `control_tables_report.py`, `control_links.py` | Extract each table's rules data (component tags, control functions, score arrays) and name it against upstream's `control.cpp` |
| `control_framework.py`, `control_annotate.py` | Map the rules module (callers, callees, strings, tags, globals) and annotate a listing for porting (message codes as `MessageCode` names, decoded floats, named fields) |
| `ft_msgmap.py` | Full Tilt message codes -> upstream `MessageCode` names, per receiving class (`re/work/spec/msgmap.json`) |
| `rules_tables_gen.py` | Generates `SpaceCadetPinball/rules/<t>_tables.cpp` from a control-table dump |
| `datfile.py`, `dat_dump.py`, `dat_check.py` | Parse and inspect `.DAT` files; replay upstream's loading rules to find what upstream would reject |
| `dat_parts.py`, `dat_attrs.py`, `dat_type_attrs.py`, `dat_layers.py`, `dat_near.py`, `dat_kicker_dirs.py` | Part types per table, attributes per part type, collision layers, parts by place or type, kicker directions and Full Tilt's normalisation of them |
| `light_sweeps.py` | Independent simulation of part type 1038 on a `.DAT` (used by the light-sweep check `re/test/*_sweep_check.py`) |
| `ghidra_scripts/ExportAll.java` | The Ghidra export script above |

## Measured facts

### The executables

All three are PE32 i386 builds of the same engine, with the same 8-section layout:

| Executable | Size | Functions | Identical to CADET.EXE | Similar (>= 80 %) | New |
|---|---|---|---|---|---|
| CADET.EXE | 499,200 | 639 | - | - | - |
| DRAGON.EXE | 505,344 | 640 | 544 (72.6 % of code) | 7 (8.1 %) | 89 / 39,951 bytes (19.3 %) |
| PIRATES.EXE | 513,536 | 655 | 546 (70.4 %) | 9 (5.9 %) | 100 / 50,473 bytes (23.7 %) |

CADET.EXE's own part (about 90 functions, 50.5 KB) is its table rules, which the decompilation
holds as `control.cpp` (4,603 lines upstream). Each Full Tilt table's rules are about the same
size again.

- The part-construction switch in the table constructor is the same in CADET.EXE and DRAGON.EXE.
  Part types 1034-1038, 1042, 1043 and several flippers per side are therefore features of Full
  Tilt's engine, present (unused) in CADET.EXE too; upstream lacks them because it follows the XP
  `pinball.exe` engine. Their classes are small: a constructor and three or four virtual methods
  each, about 5.8 KB of machine code for all six.
- Type 1034 is the plunger class built with a launcher flag; type 1037 is the ball class (a ball
  placed by the DAT).
- PIRATES.EXE's engine code equals DRAGON.EXE's apart from the table id and call targets.
- The rules are static data laid out as upstream's `control.cpp` declares them. CADET.EXE has 88
  score components and 145 plain component tags, matching upstream's `control.cpp` entry for entry;
  DRAGON.EXE has 75 / 150 and PIRATES.EXE 74 / 192, each with its own handler functions (DRAGON.EXE
  reuses none of CADET.EXE's).
- Every shared engine function the port changes is equivalent in CADET.EXE and DRAGON.EXE
  (`disasm_cmp.py`): Full Tilt's Space Cadet runs the Full Tilt engine behaviour, including where it
  differs from upstream (for example a 15 s tilt timeout, a 0.2 s plunger release boost).
- `rand()` is the MSVC generator (`state = state * 214013 + 2531011`, result bits 16-30). None of
  the three executables calls `srand`, and the initial state is 1, so the sequence starts at 1 at
  process start and is never reseeded. The engine and the rules draw from the one state.
- The originals step their physics with a `timeGetTime()` delta from an uncapped main loop (about
  1-2 ms per step) and have no fixed time step.
- The originals wait 2000 ms after showing the window before the first game starts.
- A sound's duration is stored only when its wave loads, which needs a DirectSound device; without
  audio every sound-driven timer is 0.

### The table data

Part lists (`table_objects`):

| Table | Parts | Part types | Types upstream lacks | Other differences |
|---|---|---|---|---|
| CADET | 340 | 29 | none | - |
| DRAGON | 332 | 29 | 1035 x3, 1036 x4, 1038 x1, 1043 x8 | 2 right flippers |
| PIRATES | 438 | 30 | 1034 x1, 1035 x3, 1036 x2, 1037 x1, 1038 x1, 1042 x6 | 2 left + 2 right flippers; no 1001 plunger |

- Dragon's Keep loads six music tracks, Skulduggery six; Space Cadet three. The rules ask for the
  track files in lower case; the data ships upper-case names.
- Skulduggery's folder ships `.MID` copies of its `.MDS` music; the originals play only `.MDS`.
- Five of CADET.DAT's eight kicker direction vectors are not unit length; Full Tilt's loader
  normalises them, upstream does not (`dat_kicker_dirs.py`). The port keeps upstream's values for
  Space Cadet.
