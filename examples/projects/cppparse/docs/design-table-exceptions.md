# Design: table-based exceptions (2026-10-12)

Status: done (2026-10-12), steps 1-5. Table mode is the default; `--eh=sjlj` keeps the setjmp model.
Results on the matrix sample at -O2: 278.6 ms and 70,053 bytes of code (setjmp model: 295.1 ms, 75,607 bytes).
In the map comparison, cppparse's code is 1.57 times cl's for the 43 functions both compilers emit (1.71 with setjmp).
Not done, as planned: the optimiser still keeps what handlers read in memory and runs no GVN in functions with
regions. Lifting that needs an exception edge from every call to its landing pad (section 7).

## 1. The problem in one paragraph

Today every `try`, and every block that owns an object with a destructor, costs code on the normal path,
even when nothing is ever thrown:

    lea rcx, [frame] ; call cppparse_push_frame        ; on entry to the region
    lea rcx, [frame] ; xor edx, edx ; call _setjmp      ; saves all registers into a 272-byte buffer
    test eax, eax    ; jne dispatch
    ...
    lea rcx, [frame] ; call cppparse_pop_frame          ; at every exit of the region (return, break, ...)

Because `longjmp` only brings back the callee-saved registers, such a function may not use the volatile
registers r8-r11, the optimiser may not run GVN in it, and locals read by a handler stay in memory.

In the matrix sample at -O2 (matrix + solver) there are **80 such regions**. Only 3 of them have a `catch` clause
(2 in matrix, 1 in solver); all the others exist only to run destructors. That is where cppparse's
2-7x size against cl comes from in functions such as `mtxSizeMismatchThrows` (127 vs 905 bytes) and
`mtxSingularThrows` (169 vs 988). It also explains the 4 values the `operator*` inner loop keeps in memory.

cl and clang pay **nothing** on the normal path. The compiler writes tables instead of code, and the
tables are only read when something is thrown. This design does the same.

## 2. The idea

1. For every function, the compiler writes an **unwind table entry**: "this function pushed rbx and rsi,
   then took 120 bytes of stack". With it, Windows can step from any function back to its caller and
   restore that caller's registers exactly. These are the standard Windows x64 `.pdata` / `.xdata`
   sections; cl writes them for every function too.
2. For each function with try or cleanup regions, the compiler also writes a small **range table**:
   "code from A to B belongs to a region; if an exception passes through there, continue at landing pad P".
3. `throw` calls the runtime, which **walks the stack** with the Windows functions `RtlLookupFunctionEntry`
   and `RtlVirtualUnwind`. In each frame it looks up the return address in that function's range table.
4. When a range matches, the runtime **jumps to the landing pad** with `RtlRestoreContext`. All callee-saved
   registers then hold the values they had in that frame when the call was made. The landing pad is
   ordinary code in the function: it runs the destructors, then tests the catch clauses (the same
   `cppparse_match` calls as today). If none matches, it calls `cppparse_resume` and the walk continues
   in the caller.

So a landing pad behaves like a second return point of the call. To the register allocator, a value
needed at the landing pad is just a value that lives across the call, which it already handles.

## 3. What the generated code looks like

    Before (setjmp)                              After (tables)
    ---------------                              --------------
    lea  rcx, [rbp-288]                          ; nothing: the region is a range in a table
    call cppparse_push_frame
    lea  rcx, [rbp-288]
    xor  edx, edx
    call _setjmp
    test eax, eax
    jne  dispatch
    ... body ...                                 ... body ...
    lea  rcx, [rbp-288]                          ; nothing
    call cppparse_pop_frame
    ...
    dispatch:                                    landing_pad:
    lea  rcx, [rbp-288]                            ... destructors (as today) ...
    call cppparse_pop_frame                        lea rcx, "Error" ; call cppparse_match ; jne handler
    ... destructors, cppparse_match ...            call cppparse_resume       ; not ours: continue walking

The normal path loses 2 calls on entry, 1 call at each exit and the 272-byte buffer. My estimate is about
75 bytes of code per region, or about 6 KB of the 75 KB in the matrix sample. On top of that, try
functions get r8-r11 and GVN back.

## 4. The tables

**Unwind info (standard Windows format, every function with a frame):**

    .pdata   : begin RVA, end RVA, unwind-info RVA                     (12 bytes per function)
    .xdata   : version 1, flags EHANDLER (only in functions with regions), prologue size,
               unwind codes (PUSH_NONVOL, ALLOC_SMALL/LARGE, SET_FPREG, SAVE_XMM128),
               handler RVA = cppparse_eh_handler, then our range table

Frameless leaf functions (no stack, no callee-saved register) need no entry; Windows already assumes
"return address at [rsp]" for them.

**Range table (ours, follows the handler RVA):**

    u32 magic "CPPX", u32 entry count, u32 pad count
    entry count x u32 { code offset in the function << 12 | pad number }    sorted by offset
    pad count x u32 landing-pad RVA

An entry holds from its code offset (20 bits, relative to the function's start, which the runtime takes
from the RUNTIME_FUNCTION) to the next entry's; a pad number of 0 marks code outside every region (a gap
between two ranges, and the end of the last one, which is always an entry); pad n is the n-th RVA of the
pad table. The entries are plain words, only the pad table needs relocations. The object writer builds
this from the `;@ehrange begin, end, pad` lines of the listing (2026-10-30; before that every range was
a begin, end, pad triple of RVAs, 12 bytes and three relocations: with a region per object, A3, the
tables of the own sources at -O0, 53,000 ranges, would have cost more than the code they save).

Nested regions need no extra field. The `cppparse_resume` call of an inner landing pad lies inside the
outer region's range, so the walk finds the outer pad next by itself.

`cppparse_eh_handler` is a function in the runtime that Windows may call during its own SEH dispatch
(an access violation, or a cl `throw` passing through). It always answers "continue search". The runtime
uses its address only to recognise "this frame was compiled by cppparse, its range table follows".

**COFF:** every entry needs the IMAGE_REL_AMD64_ADDR32NB relocation (image-relative; today the writer has
REL32 and ADDR64). A COMDAT function's `.pdata`/`.xdata` must go in an *associative* COMDAT section
(selection 5), so the linker drops them together with the function. cl does the same.

## 5. The frame shape

**As built (step 1):** no layout change was needed after all. `rsp` never moves in a function body, so the
unwind codes can describe the whole frame from `rsp` without a frame register. `rbp` is declared as one more
pushed register (`.pushreg rbp`), `mov rbp, rsp` needs no code of its own, and each xmm save at `[rbp-x]` is
described as `.savexmm128 xmmN, frameSize-x` (its offset from `rsp` after the prologue). The text below was the
original plan, kept for reference.



Windows unwind codes can only describe saves at **positive** offsets from the frame base. Today's
prologue sets `rbp` first and stores xmm6-15 below it (`movdqu [rbp-x], xmm6`), which the format cannot
express. The new shape is the one cl and clang use:

    push rbp                     ; UWOP_PUSH_NONVOL
    push rbx / rsi / ...         ; UWOP_PUSH_NONVOL each
    sub  rsp, N                  ; UWOP_ALLOC_SMALL / LARGE (with __chkstk for N >= 4096, as today)
    movaps [rsp+x], xmm6         ; UWOP_SAVE_XMM128 (x/16)
    mov  rbp, rsp                ; UWOP_SET_FPREG, offset 0
    ... locals at [rbp+x], positive offsets ...
    epilogue: movaps xmm6, [rbp+x] ; lea rsp, [rbp+N] ; pop ... ; pop rbp ; ret

This is the official epilogue form, so even an asynchronous unwind in the epilogue is correct. What changes is
`layoutFrame`, the prologue/epilogue code and the frame addresses in X86.cpp: most go through `localRef`,
plus about 15 places that write `[rbp-...]` directly.

A later option, not part of this project: no frame pointer at all (address through rsp, like cl). That
gives the allocator one more register.

## 6. The runtime (cppparse_rt.cpp)

    cppparse_throw / cppparse_throw_owned / cppparse_rethrow   same interface as today
      -> record the exception (as today)
      -> phase 1 (search): walk the stack without changing anything until a frame whose range table
         covers its return address is found. If there is none: "unhandled exception of type X", abort
         (as today, and before any destructor has run, as C++ allows).
      -> phase 2: walk again and RtlRestoreContext into the first landing pad.
    cppparse_resume       called by a landing pad whose clauses did not match: continue phase 2 from its caller
    cppparse_eh_handler   returns ExceptionContinueSearch
    cppparse_match / catch_begin / catch_end                   unchanged

Phase 1 cannot tell whether a landing pad will catch (its clauses are code), so "handled" means "some
cppparse frame has a region here". An unhandled exception therefore runs destructors before aborting.
That is allowed by C++ ("implementation-defined whether the stack is unwound").

`cppparse_frame_depth()` returns 0 in table mode (no frames are pushed), so the driver checks stay valid.

## 7. Optimiser and register allocator

- In table mode, a call that may throw inside a region **ends its block** with two exits: the next block
  and the landing pad (LLVM's "invoke"). Liveness, SSA phis and the allocator then see the real control
  flow, with no special cases.
- In table mode, the setjmp workarounds go away: `pinHandlerLocals`, "no GVN in try functions",
  "no volatile registers in try functions" and the `handlersSelfContained` revert.
- At a landing pad, volatile registers are garbage. That is already true after any call, and values live
  into the pad are live across the call, so they get callee-saved registers or stack slots.
- Done in the back end only (2026-10-13): the register allocator's liveness has an edge from every block
  whose calls or throws lie in a region to the handlers of that region and of the regions around it
  (X86.cpp computeEhEdges, used by computeIntervals). Without it, a value the code after a catch needs (a
  string literal hoisted out of a loop whose body catches) was dead at an inlined throw laid out after the
  loop, which reused its callee-saved register; the landing pad then resumed with the throw's value in it.
  The IR does not split blocks at calls yet, so the optimiser's workarounds stay.

## 8. Limits (same as today)

- Destructors in **cl-compiled** frames between `throw` and `catch` are not run, and a cl `throw` is not
  caught by a cppparse `catch`. Today's `longjmp` has the same limit. Full interoperation would mean
  writing MSVC's own FuncInfo / ThrowInfo / RTTI tables; that is a possible later project.
- Linux (version 7): the same model maps onto `.eh_frame` (CFI instead of unwind codes) and
  `_Unwind_RaiseException` with a personality routine. The range table and the landing pads stay the same.

## 9. Steps (each one: full suite + matrix, lib, life, selfhost, mix, cprog, mapcompare; then commit)

| Step | What | How it is tested | Visible result |
|---|---|---|---|
| 1 | New frame shape + `.pdata`/`.xdata` for every function (both modes), ADDR32NB, associative COMDAT | `dumpbin /unwindinfo` check in build-cppparse.ps1; a runtime test walks the stack from inside nested cppparse calls with `RtlVirtualUnwind` and checks every return address and restored register | Debugger call stacks and crash dumps through cppparse code become correct; no speed change |
| 2 | Runtime: two-phase walker, `cppparse_resume`, `cppparse_eh_handler` | Through step 3 | — |
| 3 | Code generator `--eh=table`: ranges, landing pads, range tables. setjmp stays the default | Every script run in both modes; outputs identical | Normal path without push/setjmp/pop |
| 4 | Optimiser/allocator in table mode: calls end blocks, setjmp workarounds removed | As step 3, at -O0 to -O3 | r8-r11 and GVN in try functions |
| 5 | Table mode becomes the default; `--eh=sjlj` kept for one version; README; size and speed tables | Full retest, mapcompare | New size/speed table against cl |

Risk: a wrong unwind code only shows up when something is thrown through that function. Step 1's stack-walk
test covers every frame shape (with and without pushes, xmm saves, large frames with __chkstk) before
step 3 depends on it.
