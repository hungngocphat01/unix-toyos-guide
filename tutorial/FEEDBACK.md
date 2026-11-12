# Feedback on the tutorial, and what it means for writing lessons

This file records every piece of feedback the owner has given about the
tutorial, how it was interpreted, and what to do or avoid because of it.
**Read it before writing or rewriting any lesson.** Add new feedback at the
end of the log, and update the checklist when a new rule comes out of it.

---

## Checklist for every lesson README

Derived from the feedback below. Go through it before calling a lesson done.

1. **It's a tutorial, not an index to the code.** The README must teach the
   topic well enough that the reader could write the code *without* opening
   `code/`. The code is for checking your work, not the main content.
2. **Explain every term the first time it appears**, in plain sentences:
   what the thing *is*, what it's *for*, and *why we need it now*. "COM1",
   "GDT", "linker script", "segment", "UART", "relocation" all need this.
   Assume the reader knows C and x86 assembly and nothing else: no
   toolchain internals, no PC hardware, no OS theory.
3. **Never hand over a complete file the reader couldn't have written.** If
   a file is needed (a linker script, a Multiboot header), first teach the
   concept it is built from, then build the file up piece by piece, saying
   why each line is there. The finished file comes at the end, as a summary.
4. **Write in full sentences and paragraphs.** No chains of jargon joined by
   colons, arrows and semicolons. No tables used as a substitute for
   explanation (a table can summarize what the prose already explained).
   One idea per paragraph. It's fine for a lesson to be long.
5. **Go from the problem to the solution.** Start each part with what we
   want and why the obvious approach doesn't work, then introduce the
   mechanism that solves it.
6. **Show, with real numbers.** Worked examples (an address computed by
   hand, a byte-by-byte hexdump, a stack drawn with real addresses from GDB)
   beat definitions. Every number shown must come from actually running the
   lesson's code.
7. **Let the reader try first.** Explain, give the task, give a way to
   check it; put the reference code in a collapsed `<details>` block after.
8. **Each step must boot or be checkable**, so the reader knows they're on
   track before moving on.

---

## Feedback log

### 1. "The tutorial is too crude. Each README is mostly an index to know what's in the code." (2026-11-12)

**Interpretation.** The old READMEs listed files, gave each one a sentence,
and linked to the code. A reader who doesn't already know the topic can't
learn from that: they end up reading the reference code and copying it,
which is exactly what the project is meant to avoid.

**Do:** treat the README as the main product. Teach the concept, then guide
the reader through writing each piece, with a check after each step.

**Avoid:** "Modules" tables that replace explanation; "the interesting part
is..." followed by a code dump; "full code: link".

### 2. "I don't know how a linker script works, so I can't write my own. You just provided a complete script in lesson 0. That was not a tutorial." (2026-11-12)

**Interpretation.** Lesson 0 treated the barebone files as givens to be
glanced at. The linker script was shown complete, with a one-paragraph
summary that assumed the reader already knew what sections, the location
counter and output sections are.

**Do:** teach what the compiler produces (object files, sections, addresses
starting at 0, relocations), what the linker does with them, what goes
wrong without a script (with a real experiment), and then write the script
line by line.

**Avoid:** presenting any file as "here it is, it does X". If the reader
couldn't reproduce the file from the lesson, the lesson isn't done.

### 3. "What's COM1 in lesson 0? What's it for? No explanation." (2026-11-12)

**Interpretation.** Hardware names were used as if they were common
knowledge. COM1, UART, Line Status Register, port I/O all appeared without
saying what a serial port is, why a 2026 kernel would use one, or how
QEMU connects it to the terminal.

**Do:** introduce each piece of hardware with what it physically is, why we
use it, how the CPU talks to it, and what we'll see as a result.

**Avoid:** naming a register or device before explaining what it is.

### 4. "In lesson 3, you didn't explain what the GDT is. You wrote the READMEs as an index to your code." (2026-11-12)

**Interpretation.** Same problem as 3, for a central concept: the lesson
about the GDT never says what segmentation is, why the CPU needs a table of
segments, or why we still have to deal with it when we'll use paging.

**Do:** when a lesson is named after a concept, the lesson's first job is
to explain that concept from zero, with its history if that's what makes it
make sense ("the CPU checks this table on every memory access because...").

**Avoid:** jumping to the table layout or the code before the reader knows
what the thing is for.

### 5. "Don't be so compact in your writing. I cannot understand what you wrote: a bunch of technical jargon linked together. This is a tutorial, not an index to your code." (2026-11-12)

**Interpretation.** The prose style itself was the problem, not just the
missing content: dense sentences packed with terms, parenthetical asides,
arrows and colons instead of explanations. Accurate, but readable only by
someone who already knows the material.

**Do:** write the way a patient teacher talks. Full sentences. Explain
cause and effect with "because" and "so". Introduce one new term at a time.
Use short examples right after an abstract statement.

**Avoid:** squeezing several ideas into one sentence; tables of terse
fragments; lists of terms with no verbs; skipping a step because "it's
obvious".
