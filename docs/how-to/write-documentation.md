# Write and organize documentation

[How-to guides](README.md)

Use this guide when adding or reorganizing public CXBX documentation.

1. Identify the reader's immediate need and choose a section:

   | Need | Section | Include |
   |---|---|---|
   | Learn by doing | `tutorials/` | A bounded exercise, prerequisites, steps, and observable results |
   | Complete a task | `how-to/` | A concrete goal, prerequisites, actions, and success or failure checks |
   | Look up facts | `reference/` | Settings, interfaces, formats, constraints, and supported behavior |
   | Understand why | `explanation/` | Context, tradeoffs, architecture, and rationale |

2. Give the page one primary purpose. Split procedures from format tables or
   extended design discussion, and link the related pages. Keep each contract
   authoritative in one place.
3. Verify commands and paths against source and CLI help. Say which directory
   to run commands from, use shell-correct syntax, and label illustrative paths
   and output. Keep machine-specific configuration and private assets out of
   public examples.
4. Distinguish implemented behavior, dated observations, and future plans.
   Place archival records under `reference/history/`; do not turn old results
   into current verification claims.
5. Add the page to its section's `README.md` and link back to that index. When
   moving an existing page, update in-repository links and remove the old page.
6. Check relative links and section anchors, verify example flags, and review
   `git diff --check`. Report which examples you actually ran.

For the underlying distinction between these forms, see
[Diátaxis: Start here](https://diataxis.fr/start-here/).
