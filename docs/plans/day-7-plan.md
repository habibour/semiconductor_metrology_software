# Day 7 plan — README and video final, CV final, submit

Read `docs/specs/day-7-spec.md` first. No code changes today beyond README/docs
polish — this is a verification and packaging pass over Days 1–6's actual output.

## 1. Pull real numbers before writing anything

1. Open `docs/benchmarks.md` (Day 6) and copy the actual measured numbers —
   queue throughput before/after, analysis time before/after, codec
   throughput, coverage percentage — into a scratch list. Do not paraphrase or
   round in a way that changes the claim.
2. Pull the actual CI run results (pass/fail counts, sanitizer job status) from
   the latest green run on `main`.
3. Pull XT-OCTAVE-1's actual % difference and XT-SECSGEM-1's actual pass/fail
   from Day 5/6.

## 2. README finalization (`README.md`)

1. Plain-English summary (reuse/tighten PRD §0's washing-machine analogy style —
   don't copy PRD text verbatim, write it for a reviewer who won't read the PRD).
2. Architecture diagram — either the ASCII from PRD §6.1 or a cleaned-up image
   version.
3. Quickstart — copy the *actual* working commands (not the PRD Appendix A
   placeholders) and time a clean-checkout run; target <10 minutes (NFR-DOC-1).
   If it doesn't hit 10 minutes, say the real time, don't round down.
4. Results table — the numbers pulled in step 1, with units and the measurement
   method noted (matches PRD §10.3 "README numbers come from real runs").
5. Limits section — state plainly: not a certified/complete GEM implementation
   (N1), no real hardware (N2), etc. — reuse PRD §3.2 non-goals as the basis.
6. "How to explain this in an interview" note — a short, honest paragraph
   Habib can actually say out loud, pointing at the traceability table (PRD
   §15) ideas: threading model, SECS/GEM subset boundary, Stoney validation,
   patterns used, what was cut and why (tiers/cut order).
7. CI badges (macOS/Linux, TSan, ASan/UBSan), test count, benchmark
   table linking to `docs/benchmarks.md`.
8. Third-party licence list per NFR-LIC-1 (Qt LGPL+dynamic linking, Asio Boost
   licence, GoogleTest BSD, nlohmann/json MIT, stb public domain).
9. Grep the whole repo for "Frontier", "FSM", any SEMI standard text, and any
   WPF/WinForms/MFC/"certified" claim that wasn't actually built and run
   (NFR-IP-1, CLAUDE.md §11) — fix any hit before proceeding.

## 3. Demo GIF + video

1. Record the storyboard from PRD §12.1: panel start → switch to Remote → run
   host script → watch a scan → show result + map → trigger a sensor alarm →
   clear it → show tests/CI badges.
2. Trim to ≤2 minutes; upload unlisted; link from README and CV.
3. Re-embed a short GIF excerpt in the README itself.

## 4. Docker / release sanity check

1. Re-run `docker compose up` (ideally on a machine/VM that hasn't built the
   project before) and confirm the one-command demo still works exactly as
   Day 6 left it.
2. Confirm the tagged release (`v0.3`) artifacts are attached and downloadable
   for macOS and Linux.

## 5. CV finalization (Appendix B rules)

1. Fill every bracketed placeholder in the Appendix B draft bullets with the
   real numbers from step 1 — never leave a placeholder in the final CV.
2. Apply the CV tasks from PRD §13 / open questions: Q5 (internship bullets
   rewritten against the posting), Q7 (competitive-programming profile /
   travel line, only if true), Q4 (drop web-related academic projects if
   advised), thesis kept as the research-based-project line.
3. Check against the "Never do" list (CLAUDE.md §11 / PRD Appendix B rules):
   no WPF/WinForms/MFC/certified claims unless actually built and run; SECS/GEM
   described as "a subset based on publicly documented behaviour"; posting's
   own vocabulary used in the skills section.
4. One page, one column, plain headings, text-based PDF or DOCX; repo + video
   links as plain text in the header.

## 6. Final Definition-of-Done pass (PRD §10.3) and traceability check (PRD §15)

1. Walk every bullet in PRD §10.3 and confirm it's actually true today, not
   aspirationally true.
2. Walk every row of the PRD §15 traceability table and confirm the "evidence a
   reviewer can see" column still points at something real and reachable
   (repo path, README section, video timestamp).

## 7. Submit

1. Submit the application with CV + repo link + video link.
2. Note the submit date/time somewhere (e.g. update CLAUDE.md's milestone table
   status for D7 to "done" with the actual date).

## Day 7 done-checklist (maps back to day-7-spec.md)

- [ ] README finished with only measured numbers; quickstart timed <10 min on a
      clean checkout.
- [ ] Demo GIF + ≤2-minute video linked from README and CV.
- [ ] CI badges, test count, benchmark table all accurate and current.
- [ ] No proprietary names/logos, no SEMI standard text, no unbuilt-claim
      (WPF/WinForms/MFC/certified) anywhere in README or CV.
- [ ] `docker compose up` re-verified working.
- [ ] CV finalized, one page, no placeholders left in.
- [ ] PRD §10.3 Definition of Done and §15 traceability table both re-walked
      and confirmed true.
- [ ] Application submitted; date recorded.
