# ST20 C2/C4 Instruction Reference

[JSON reference](ST20_C2_C4-Core-Instruction-Set-Reference-Manual--72-TRN-273-01.json)
transcribed from the adjacent
[PDF manual](ST20_C2_C4-Core-Instruction-Set-Reference-Manual--72-TRN-273-01.pdf),
document 72-TRN-273-01, January 1996.

## Contents

- `source`: document identity, source SHA-256, target cores and page counts.
- `transcription`: scope, notation, normalization rules and limitations.
- `instruction_components`: `pfix`, `nfix` and `opr`, summarized from chapter 4.
- `reference_pages`: chapters 1-4, PDF pages 5-30, including notation, constants,
  registers, memory addressing and encoding rules.
- `instructions`: all 171 alphabetical entries from chapter 5, with continuation
  pages included. There are 13 primary and 158 secondary instructions.

Each instruction contains `mnemonic`, `syntax`, `source_syntax`, `name`,
`instruction_class`, `encoding`, `description`, `definition`, `definition_ir`, `error_signals`,
`comments`, `see_also`, `see_also_text`, `source_sections`, `source_pages` and
`extraction_warnings`. `source_sections` preserves ordered headings, including
duplicates. `source_pages` preserves cleaned page text and both PDF and printed
page numbers. Missing source sections are represented by `null` where applicable.

`encoding.source_text` retains the manual's encoding. Primary instructions have a
hexadecimal `function_nibble`, with `bytes_hex` and `opr_operand` set to `null`
because the operand determines the complete byte sequence. Valid secondary
encodings have a `bytes_hex` array and a signed decimal 32-bit `opr_operand`.

Definitions are source pseudocode, not executable code. Primed values denote
post-execution state; unprimed values denote pre-execution state. Statement order
does not specify execution order. Consult the supporting pages for notation.

## Structured Definitions

Document schema version 2 adds `definition_ir` alongside unchanged `definition`
text. A missing definition remains `null`. Each AST has `version: 1`,
`semantics: "state_relation"`, `statements`, optional `bindings`, `source_text`,
`source_pdf_page`, `completeness` and `opaque_count`.

Every AST node uses a `node` discriminator:

| Node | Payload fields |
| --- | --- |
| `assign` | `target`, `value` |
| `if` | `condition`, `then`, `else` (branches are statement arrays) |
| `for` | `variable`, `from`, `to`, `body` (inclusive bounds) |
| `while` | `condition`, `body` |
| `ref` | `name`, `state`, optional `index`, optional `type` |
| `literal` | `value`, `type` (`integer`, `boolean` or `string`) |
| `constant` | `name` |
| `undefined` | No payload; distinct from JSON `null` |
| `unary` | `operator`, `operand` |
| `binary` | `operator`, `left`, `right`, optional `arithmetic`, optional `rounding` |
| `memory` | `unit`, `address`, `state` |
| `slice` | `value`, `low`, `high` (inclusive bit bounds) |
| `call` | `name`, `arguments` |
| `signal` | `name` |
| `action` | `text` (uninterpreted prose action) |
| `predicate` | `text` (visually legible prose condition, uninterpreted) |
| `prose` | `text` (visually legible prose value, uninterpreted) |
| `annotation` | `text` (source note, such as helper precision) |
| `recurrence` | `name`, `parameter`, `initial`, `from`, `to`, `value`, `arithmetic`, `word_bits` |
| `opaque` | `reason` (unresolved source retained in `source_text`) |

Nodes carry `source_text` and `source_pdf_page`. Root `source_text` exactly equals
the original `definition`; visually reviewed AST nodes use the separately saved
`normalized_source_text`, with normalization evidence in `visual_review`.
Wrapped expression fragments may have normalized spacing.
`ref.state` is `before` for unprimed state, `after` for primed state, or
`local` for helper names and loop variables. Unprimed assignment targets are
flagged as opaque unless a page-specific, recorded editorial repair restores
the omitted marker using the assignment and instruction description.

Bindings are declarative `where` helpers with `name`, optional `parameters`,
`value`, optional `annotations`, and source provenance. Unsupported recurrences
remain opaque binding entries. They are not imperative assignments or an
execution sequence. Annotations preserve the source's unlimited or double-word
precision requirements. `ref.type: "unsigned"` preserves unsigned type labels.

CRC `recurrence` bindings specify a seed in `initial.argument` and `initial.value`,
an inclusive iteration range, and a step expression using the local `parameter`.
Each generated value is reduced modulo the word width specified by `word_bits`;
the CRC bindings use `arithmetic: "word_modulo"` and 32 bits. This preserves the
ordered mathematical dependency on the previous helper value, without changing
the snapshot semantics of instruction register assignments.

`binary.arithmetic: "checked"` preserves checked arithmetic. Fractional multiply
uses `operator: "divide"` with `rounding: "nearest_ties_even"`; the printed
`roundnearest` subscript modifies division, not the denominator.

Binary operator names include `add`, `subtract`, `multiply`, `divide`,
`remainder`, `word_offset`, `shift_left`, `shift_right`, `bit_and`, `bit_or`,
`bit_xor`, `and`, `or`, `eq`, `ne`, `lt`, `le`, `gt`, `ge`, `after` and `power`. Unary
operators are `negate`, `not` and `bit_not`. Recognized expressions use conventional
arithmetic precedence, bitwise operations before comparisons, and comparisons
before Boolean operations. `power` is right-associative and binds more tightly
than unary negation. Visually confirmed range comparisons are normalized to
conjunctions; other chained comparisons remain opaque.

Normalized pseudocode uses `value:unsigned` for type labels, `bit(value, index)`
and `bits(value, low, high)` for selectors, and `^` for powers. Numeric descending
bit ranges are stored with ascending low/high bounds. `predicate("text")` and
`prose("text")` preserve source language without supplying an implementation.
`popcount` and `reverse_bits` are descriptive helper names, not executable code.

`completeness: "structured"` means no opaque fragments remain, not that executable
semantics have been established or PDF typography visually verified. `partial`
means opaque nodes are present; inspect their reasons and source text. Ambiguous
nesting preserves the entire definition as one opaque statement rather than
making branch-local effects unconditional. No AST is intended to be executed
directly, and array order does not imply machine-state update order.

## Caveats

The full JSON has now been compared visually against the PDF: all 171 instruction
entries on pages 32-206 and all 26 supporting pages on pages 5-30. All 213 PDF
pages were inspected, with omitted front matter, index and back matter checked
for scope. Original extracted text
is unchanged and may still flatten mathematical typography. Use the PDF and
the JSON's review metadata together; the manual itself contains contradictions.

This full pass corrected `csub0`'s spaced exception label and joined wrapped prose
actions in `enbc`, `out` and `signal`. In `signal`, joining the continuation also
restored the entire scheduling action to its visible `else` branch.

Reported source conflicts include `rem`'s reversed formula operands, `diss`'s
first-guard branch, `devss`'s bit range, `ldshadow`'s current-priority exception,
and out-of-range `shl`/`shr` results. These are preserved, not silently corrected.
Other source qualifications include missing prefix-formula comparison symbols on page 28,
flattened diagram/exponent notation, and register-name capitalization variations.

Review provenance, normalization rules and editorial repairs are retained in
`definition_ir.visual_review`. Corrections apply only to the AST's normalized
input, not the original transcription.
Wrapped sidebar comments in `disc` and `dist` are restored to their visually clear
branch layout; comments remain in the original `definition` and page text.

There are now 170 structured definitions, zero partial definitions and one absent
definition. The 13 formerly partial entries retain `visual_review.source_issues`
and are marked `visual_review.editorial_repair: true`, with an explicit `reason`.
These are reviewed interpretations, not claims that the repaired notation was
printed in the PDF. Eight omitted target primes are restored from explicit
assignments; the unsigned comparison label and missing delimiters are repaired
using their descriptions and neighboring statements.

The two CRC formulas reconstruct the omitted outer opening parenthesis around
the shifted checksum plus incoming bit, before XORing the polynomial selected by
the previous checksum's outgoing bit. The description specifies that algorithm;
independent bitwise tests verified both 8- and 32-step ASTs during transcription. Their original broken
formulas remain unchanged in `definition` and source page text.

Structured prose nodes remain descriptions, not executable semantics or evidence
that all processor behavior is fully specified. Structural completeness and
editorial confidence are separate; inspect repair metadata before treating an AST
as an authoritative implementation specification.

The extracted `jn` title is normalized to `j n` for lookup, based on its description
and definition; `source_syntax` retains `jn`. Other suspicious spellings and
cross-references are preserved and flagged. In particular, `taltwt` has extracted
code `25 FI` (also confirmed visually), so its byte sequence and decoded operand are `null`. `ldlp` has a
repeated empty definition heading; `reboot` has repeated error headings and no
definition heading. These are retained, not silently repaired. Warnings are not
an exhaustive list of source errata or extraction defects.

## Usage

The JSON is a standalone reference. The one-time extraction, conversion, testing
and visual-audit artifacts have been removed; no generation tooling is required
to read it. The original PDF is retained for checking source notation and defects.

Example lookup with `jq`:

```sh
jq '.instructions[] | select(.mnemonic == "add")' \
  manuals/ST20/ST20_C2_C4-Core-Instruction-Set-Reference-Manual--72-TRN-273-01.json
```