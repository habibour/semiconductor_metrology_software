# Day 7 spec — README and video final, CV final, submit

Source of truth: `docs/PRD.md`. This file is a filtered index into it.

PRD §13, row D7: "README and video final; CV final; submit." Exit: "Application
sent." Buffer day (28 Sep) follows with no planned work — emergencies only.

Nothing is built today. This day is entirely about presenting, verifying, and
shipping what Days 1–6 actually produced — every number, claim and bullet must
trace back to something measured or run earlier in the week (CLAUDE.md §1 "say
clearly what was verified and what was assumed"; PRD §9 rule 7 "never invent
numbers, benchmark results, coverage figures, bug stories or interview claims").

## In scope today

| ID / section | What it requires | PRD ref |
|---|---|---|
| 12.1 | README: plain-English summary, architecture diagram, quickstart, results table, limits, "how to explain this in an interview" note | PRD §12.1 |
| 12.1 | Demo GIF + ≤2-minute unlisted video, linked from README and CV; storyboard: panel start → switch to Remote → run host script → watch scan → show result/map → trigger alarm → clear it → show tests/CI badges | PRD §12.1 |
| 12.1 | CI badges (three OS, sanitizers), test count, benchmark table | PRD §12.1 |
| 12.2 | Confirm the Level 2 release and local demo deliverables from Day 6 are actually linked and working from the README | PRD §12.2 |
| NFR-DOC-1 | README quickstart verified to work from a clean checkout in under 10 minutes — actually time it | PRD §9 |
| NFR-LIC-1 | Own code under a permissive licence (set Day 1); third-party licences listed (Qt LGPL+dynamic linking, Asio Boost licence, GoogleTest BSD, nlohmann/json MIT, stb public domain) | PRD §9 |
| NFR-IP-1 | No Frontier/FSM names, logos or proprietary material; no SEMI standard text; SECS/GEM described as "a subset based on publicly documented behaviour" | PRD §9 |
| 10.3 | Definition of Done (project level) — run through every bullet as a final checklist | PRD §10.3 |
| §15 | Traceability table to the job posting — sanity-check every row still points at something real | PRD §15 |
| Appendix A | Build/run/test instructions verified to actually work as written, on a clean checkout | PRD Appendix A |
| Appendix B | CV bullets finalized with real measured numbers; rules: no WPF/WinForms/MFC/"certified" claims unless true; SECS/GEM described as a subset; posting's own vocabulary used in skills section; one page, plain headings | PRD Appendix B |
| §13 CV tasks | Internship bullets rewritten against the posting (Q5), competitive-programming rating/count if available (Q7), thesis kept as the "research-based project" line, travel willingness stated only if true (Q7), web-related academic projects dropped if advised (Q4) | PRD §13 |

## Tests / verification today

No new automated test IDs. Verification is manual/inspection (verification key
"D" throughout PRD §10.3):

- Clean-checkout timing of the README quickstart (target: under 10 minutes).
- `scripts/demo.sh` re-run from a fresh clone.
- Every README number cross-checked against `docs/benchmarks.md` / actual CI
  run results from Day 6 — no number typed in from memory.
- Video watched back at real length to confirm it's ≤2 minutes and matches the
  storyboard.

## Exit criteria (verbatim from PRD §13, row D7)

> README and video final; CV final; submit.

Followed by PRD §13's buffer day (28 Sep, deadline): no planned work, emergencies
only.
