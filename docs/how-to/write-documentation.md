# Write and organize documentation

[How-to guides](README.md)

Use this guide when adding or reorganizing public CXBX documentation.

1. Identify the reader's immediate need and choose a section:

   | Reader's question | Section | Include |
   |---|---|---|
   | Can you guide me through practising this? | `tutorials/` | A bounded exercise with one prescribed route and visible results after each step |
   | How do I achieve this result? | `how-to/` | A real task, prerequisites, actions, necessary choices, and success or failure checks |
   | What are the exact rules? | `reference/` | Settings, interfaces, formats, constraints, and supported behavior organized by the system being described |
   | Why does it work this way? | `explanation/` | Context, tradeoffs, subsystem relationships, and design rationale |

2. Match the writing to that need. In a tutorial, choose the example and guide
   the learner through it with expected results. In a how-to guide, assume the
   reader knows the basic workflow and help them complete their task. Keep
   reference factual and easy to scan. Use explanation to connect decisions
   with their reasons and consequences.
3. Give the page one primary purpose. Split procedures from format tables or
   extended design discussion, and link the related pages. Keep each contract
   authoritative in one place. For example, capture collection, comparison
   between runs, and offline inspection have separate task guides that share
   the NV2A capture reference.
4. Verify commands and paths against source and CLI help. Say which directory
   to run commands from, use shell-correct syntax, and label illustrative paths
   and output. Keep machine-specific configuration and private assets out of
   public examples.
5. Distinguish implemented behavior, dated observations, and future plans.
   Place archival records under `reference/history/`; do not turn old results
   into current verification claims.
6. Add the page to its section's `README.md` and link back to that index. When
   moving an existing page, update in-repository links and remove the old page.
7. Check relative links and section anchors, verify example flags, and review
   `git diff --check`. Follow a tutorial's complete sequence to check its
   promised results. For other pages, verify the commands or contracts that
   changed. Report which examples you actually ran.

For the underlying distinction between these forms, see
[Diátaxis: Start here](https://diataxis.fr/start-here/).
