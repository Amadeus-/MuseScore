# MuseScore — Contributing & Conventions

> **What this file is.** Scope: how to contribute to MuseScore (workflow, PR template), the **code style** actually enforced (uncrustify), the **AI policy** (`AI_POLICY.md`, incl. the `Assisted-by:` trailer), commit/PR conventions, the Code of Conduct, and the fork↔upstream relationship. Every claim below was read from the live repo files at `C:\Dev\+MuseScore+` on 2026-11-08. **If this contradicts the live source, the source wins.** Start at `00-core.md`.

## 0. 60-second orientation

- `CONTRIBUTING.md` is a **one-sentence pointer** to the upstream wiki: <https://github.com/musescore/MuseScore/wiki/Contributing> (`CONTRIBUTING.md:1`). All real contribution process lives on that wiki (not in the repo, not fetched here).
- Style is enforced by **uncrustify 0.74** (`muse\tools\codestyle\uncrustify_muse.cfg:2`), **not** clang-format. There is **no root `.clang-format`**; the only one in the tree is third-party (`muse\framework\dockwindow\thirdparty\KDDockWidgets\.clang-format`).
- CI job **`Check: Codestyle`** (`.github\workflows\check_codestyle.yml`) re-formats `./src/` and `./muse/framework/` and fails the PR if `git diff` is non-empty.
- ⭐ **AI policy:** AI use is allowed; you are fully accountable; if AI produced a *substantial portion* you must tick the PR-template AI field, name the tool, and add the commit trailer **`Assisted-by: <tool name> <version, if known>`** — **never `Co-authored-by:`** (`AI_POLICY.md:59-67`). See §3 for the clash with the user's usual `Co-Authored-By: Claude …` trailer.
- Remotes: `origin` = `https://github.com/Amadeus-/MuseScore.git` (user's fork), `upstream` = `https://github.com/musescore/MuseScore` (`git remote -v`). Single local branch `main` tracking `origin/main`.

## 1. Contributing workflow (fork → branch → PR → wiki)

1. **Read the wiki** Contributing page (above). `CONTRIBUTING.md` itself says nothing more.
2. **Fork** `musescore/MuseScore` (here: `Amadeus-/MuseScore`), branch off `main`, make focused commits.
3. **Sign the CLA** — <https://musescore.org/en/cla> (PR template `.github\pull_request_template.md:7`). Source files carry the marker `MuseScore-Studio-CLA-applies` right under the SPDX line (e.g. `src\notation\inotation.h:3`, `hooks\pre-commit:3`).
4. **Open the PR** against `musescore/MuseScore` using the template; fill the checklist truthfully — the template states *"failure to complete this checklist accurately will result in the PR being closed"* (`pull_request_template.md:5`).
5. Pass CI (build matrix, unit tests, vtests, codestyle — see `ci-and-release.md`); respond to review.

### PR template (`.github\pull_request_template.md`, read in full)

| Line | Item |
|---|---|
| 1 | `Resolves: #NNNNN` — GitHub issue number or direct link if the issue isn't on GitHub |
| 3 | short description + motivation |
| 7 | signed the [CLA](https://musescore.org/en/cla) |
| 8 | PR **title describes the problem** it addresses |
| 9 | follows [the coding rules](https://github.com/musescore/muse_framework/wiki/CodeGuidelines) |
| 10 | no unnecessary changes |
| 11 | each commit message describes purpose+effects **and references the issue**; extensive change = sequence of easily reviewable commits |
| 12 | compiles and runs, preferably **after each commit individually**; manually tested |
| 13 | no prior attempts — or list them and say how you avoid past mistakes |
| 15-16 | *if applicable:* unit test **or vtest** added |
| 17 | *if applicable:* **AI-assistance checkbox** — "created using AI assistance; I have read, understood and complied with the guidelines" |
| 19 | comment slot: **list all AI tools used** |

Other `.github` content: `ISSUE_TEMPLATE\{bug-report,feature_request}.yml`, `config.yml`, `issue_add_label_config.yml`, `issue_assign_by_label_config.yml`, `team_members.txt`, `dependabot.yml`, `problem_matchers\{gcc,msvc}.json`, and ~20 workflows (`triage_issues.yml`, `triage_prs.yml`, `check_submodules.yml`, …).

## 2. Code style & formatting

### 2.1 What enforces it

| Piece | Path | Role |
|---|---|---|
| Config | `muse\tools\codestyle\uncrustify_muse.cfg` | the one style definition (uncrustify 0.74); header comment points at the (external) `musescore/Documentation` `CodeGuidelines.md` (`:5`) |
| Per-file runner | `muse\tools\codestyle\format_file.cmake` | `uncrustify -c <cfg> --no-backup -l CPP <file>` |
| Per-dir runner | `muse\tools\codestyle\format_dir.cmake` + `run_scan.sh` | `find -L <dir>`, **prunes any dir containing `.untidy`**, runs uncrustify on `*.cpp *.c *.cc *.hpp *.h` (`-l CPP`) with `xargs -P 16`, then `*.mm` (`-l OC+`) (`run_scan.sh:11-30`) |
| Tool fetch | `muse\tools\codestyle\uncrustify.cmake` | locates `muse_deps` (clones it + checks out `muse\buildscripts\muse_deps.ref` if absent), `require_tool(uncrustify)` from its manifest |
| CI driver | `muse\buildscripts\ci\checkcodestyle\checkcodestyle.cmake` | runs `format_dir.cmake` on the scan dir, then fails if `git diff --name-only` is non-empty (`:25-52`) |
| CI workflow | `.github\workflows\check_codestyle.yml` | on `pull_request` + `workflow_dispatch`; steps *Test MSS coding style* (`./src/`) and *Test MF coding style* (`./muse/framework/`) (`:21-27`). A twin exists in `muse\.github\workflows\check_codestyle.yml`. |
| Local single-file | `tools\codestyle\tidy_file.sh` | by extension: `c`→C, `h/cpp/hpp`→CPP, `m`→OC, `mm`→OC+; rewrites only if output differs; config path hard-wired to `muse/tools/codestyle/uncrustify_muse.cfg` (`:19`) |
| Scope lists | `tools\codestyle\globals.source` | `TIDY_EXTENSIONS=(c cpp h hpp m mm)`, `TIDY_DIRS=(src)`, `UNTIDY_FILE='.untidy'` |
| Git hook | `hooks\pre-commit`, `install.sh`, `uninstall.sh` | optional; `install.sh` just **copies** `hooks\pre-commit` to `.git\hooks\` (`install.sh:29-31`). The hook tidies **staged** `src/**` C/C++/ObjC files (skipping `.untidy` dirs) and re-`git add`s them, protecting unstaged edits via `.unstaged` temp files (`pre-commit:21-118`). **It auto-formats** (resolves the old UNCLEAR). |
| Cherry-pick helper | `tools\codestyle\cherry_pick.sh` | cherry-picks across branches with different style: checks out the pre-commit file versions, tidies, commits, then applies the real change |

`.untidy` marker files (= third-party, not formatted): `src\engraving\thirdparty`, `src\importexport\{capella,mei,midi}\thirdparty`, `muse\framework\{audio,dockwindow,global,media}\thirdparty`, `muse\framework\uicomponents\qml\Muse\UiComponents\LegacyTreeView`.

⚠ **Not identical coverage:** the git hook covers only `src\`; CI covers `src\` **and** `muse\framework\`. A `muse\framework` edit won't be auto-tidied locally by the hook but **will** be checked in CI.

### 2.2 What the config actually enforces (read from `uncrustify_muse.cfg`)

Hybrid "K&R for control flow, Allman for functions/classes" — **this conflicts with the user's global Allman-everywhere style.**

| Area | Setting (cfg line) | Effect |
|---|---|---|
| Indent | `indent_columns=4` (52), `indent_with_tabs=0` (62) | 4 spaces, never tabs |
| Namespace body | `indent_namespace=false` (94) | namespace contents **not** indented (see `inotation.h:35-41`) |
| Class body | `indent_class=true` (107); `indent_access_spec=-4` (196) | members indented; `public:`/`private:` flush with `class` |
| Switch | `indent_switch_case=0` (173) | `case` aligned with `switch` |
| Control braces | `nl_if_brace/nl_else_brace/nl_for_brace/nl_do_brace/nl_switch_brace/nl_elseif_brace=remove` (993-1055), `nl_brace_else=remove` (996) | **K&R: `if (x) {` on same line; `} else {`** |
| Function braces | `nl_fdef_brace=force` (1152) | **function-definition `{` on its own line** |
| Class braces | `nl_class_brace=force` (1083) | `class X` then `{` on next line |
| Namespace braces | `nl_namespace_brace=remove` (1077) | `namespace x {` same line |
| Mandatory braces | `mod_full_brace_if/for/while/do=force` (1400-1419) | uncrustify **adds braces** to unbraced one-line `if/for/while/do` |
| One-liners | `nl_func_leave_one_liners`, `nl_getset_…`, `nl_cpp_lambda_…`, `nl_assign_…=true` (907-922) | `int f() { return 0; }` and one-line lambdas are left alone; one-line `if` are split (`nl_if_leave_one_liners=false`) |
| Width | `code_width=140` (1288) | soft wrap target 140 cols |
| Spacing | `sp_before_sparen=force` (378), `sp_inside_paren/sparen/fparen=remove`, `sp_arith/bool/compare=force`, `sp_after_comma=add`, `sp_cond_*=force`, `sp_return_paren=force` | `if (a && b)`, `a + b`, `f(x, y)`, `return (x);`→ but `mod_paren_on_return=remove` (1425) strips needless parens |
| Pointers/refs | `sp_before_ptr_star=remove`, `sp_after_ptr_star=force`; same for `&` | `Type* p`, `const T& r` (hugs the type) |
| Templates | `sp_template_angle=remove`, `sp_angle_word=force` | `template<typename T>`, `List<int> x` |
| Assignment spacing | `sp_assign=ignore` (248) | not enforced |
| Wrap positions | `pos_arith/assign/bool/compare/conditional=lead` (1255-1268); `pos_comma=trail` | operators lead the continuation line; commas trail |
| Ctor init lists | `nl_constr_colon=force` (1281), `pos_constr_colon=lead`, `pos_constr_comma=trail` | `Foo::Foo()` newline `: m_a(a)` newline `, m_b(b)` |
| Blank lines | `nl_max=2` (1304), `nl_after_func_body=2` (1313), `eat_blanks_after_open_brace/before_close_brace=true` (1371-1374) | max 2 consecutive; **2 blank lines after a multi-line function body**; no blank after `{` / before `}` |
| File ends | `nl_end_of_file=force` (937), `nl_start_of_file=remove` | exactly one trailing newline |
| Misc | `mod_remove_extra_semicolon=true` (1434) | strips stray `;` |
| Not touched | `align_*` all 0/false; `cmt_*` off; `pp_indent=ignore`; `nl_before_return=false`; `mod_remove_empty_return=false` | no column alignment; comments/preprocessor left as written; `return;` kept |
| Qt | `set FOR foreach` / `set FOR forever` (10-11) | treats Qt `foreach`/`forever` as loops |

**Naming/other conventions are NOT enforced by the cfg.** Observed in source (e.g. `src\project\internal\notationproject.cpp:76-108`, `src\notation\inotation.h`): `m_` prefix for members, `camelCase` methods, `PascalCase` types, `I`-prefixed interfaces, `#pragma once` guards, `mu::`/`muse::` namespaces, SPDX+CLA license block at the top of every file, `// IWYU pragma: export` annotations. The authoritative human-readable rules are on the external wiki (the cfg header points to `musescore/Documentation` `WorkflowAndGuidelines/CodeGuidelines.md`; the PR template points to `muse_framework/wiki/CodeGuidelines` — two different URLs, neither fetched; UNCLEAR which is current).

### 2.3 Reconciling with the user's global style (CRITICAL)

The user's global style (Allman braces **everywhere**, fixed-width lowercase int types, `nullptr`, explicit trailing `return;`, ~180-col lines, no cosmetic wrapping) **conflicts with uncrustify** in at least: control-statement brace placement (uncrustify = same line), `nl_after_func_body=2`, 140-col wrap, and `eat_blanks_*`. Rules:

1. **Author-private / fork-local code** (never going upstream): follow the user's global style.
2. **Upstream-bound edits:** match the **surrounding file's existing style** and make sure the file survives `uncrustify -c muse/tools/codestyle/uncrustify_muse.cfg -l CPP` unchanged — otherwise `check_codestyle` fails the PR. Do not reformat unrelated lines (PR checklist: "no unnecessary changes").
3. **Ambiguous → flag and ask**, don't silently pick.
4. Even in upstream-bound code, items the cfg does *not* control (e.g. using `int32_t`, `nullptr`, `return;`) are not forbidden — but follow what neighbouring code does; MuseScore code uses plain `int`/`size_t` widely, so a mass swap would be an "unnecessary change".
5. A uncrustify run on a file can reformat the **whole file**; run it only on files you touched and check `git diff` stays confined to your change.

## 3. AI policy (`AI_POLICY.md`, read in full) — and the trailer difference

Two copies exist: **`AI_POLICY.md`** (root, MuseScore-specific) and **`muse\AI_POLICY.md`** (the framework submodule's generic template). They are **textually identical except the project name**: root says "MuseScore Studio" (e.g. `AI_POLICY.md:4,21,43,67,84,89`) where `muse\AI_POLICY.md` says the placeholder "Project" (same line numbers). Substance and numbering are the same; the root file governs this repo.

**Scope (`:3-5`):** any AI use (LLMs, coding assistants, autonomous agents) in code, tests, issues, bug reports, PR descriptions, review comments, docs, translations. Rationale (`:7-16`): agents shift the bottleneck from writing to **reviewing**; maintainers are responsible for understanding every bit of the product.

| § | Rule |
|---|---|
| 1 `:18-24` | **AI-assisted contributions are allowed.** Same standards for correctness, maintainability, licensing, testing, review regardless of origin. |
| 2 `:26-47` | **You are accountable.** Read, review, understand *all* LLM output incl. code comments before requesting review; be able to answer questions. **Strongly recommended:** write PR descriptions yourself (translation/copy-edit tools OK). Don't submit unreviewed output. **Don't paste maintainer feedback into an AI and submit its reply.** A **human must review/understand/approve** any agent action that opens PRs, creates issues, posts review comments, pushes, or touches repos/trackers/community spaces. Maintainers may close/postpone/decline contributions needing disproportionate review effort, AI or not. |
| 3 `:49-72` | **Disclosure** when AI produced a *substantial portion*. Not required for autocomplete, boilerplate, spelling/grammar, light copy-edit, translating your own text. For PRs with code/tests/docs: **(a)** tick the AI-assistance field in the PR template and name the tool; **(b)** add the trailer to the relevant commit(s). Issues/non-commit work: disclose in the issue/template field/description. Line-level attribution not needed. *When in doubt, disclose.* |
| 4 `:74-92` | **Copyright / licensing / CLA.** You must have rights to everything contributed; AI doesn't make copied/regenerated third-party material safe to relicense; verify license compatibility if output resembles an identifiable source; **don't use AI to disguise/rewrite material to dodge its license**; sign the CLA as usual — AI doesn't change your CLA representations. |
| refs `:94-102` | adapted from LLVM AI Tool Policy, Fedora AI-assisted-contributions policy, Apache generative-tooling guidance. |

### ⭐ The exact trailer requirement (`AI_POLICY.md:59-67`)

```
Assisted-by: <tool name> <version, if known>
```

> "Use `Assisted-by:`, not `Co-authored-by:`. An AI tool cannot take responsibility for a contribution or sign MuseScore Studio's CLA." (`:66-67`)

Real examples in this repo's history (`git log --grep=Assisted-by` matches ~49 recent commits, so it is actively used):
- `Assisted-by: OpenAI Codex` (`fe4f0f7a99`, "Expose instrument transposition to plug-ins (#30534)")
- `Assisted-by: Claude Code 2.1.287 (Claude Opus 5.5)` (`71845ea95c`, "Fix #35116: …")

### ⚠ Difference vs the user's normal trailer

| | Trailer |
|---|---|
| **User's global rule / harness default** | `Co-Authored-By: Claude <model> <noreply@anthropic.com>` |
| **MuseScore upstream requires** | `Assisted-by: Claude Code <version> (<model>)` |

The two are **not interchangeable**: MuseScore explicitly forbids `Co-authored-by:` for AI. For any commit/PR **bound for `musescore/MuseScore`**, use `Assisted-by:` (and tick the PR-template AI box + list the tool). For fork-only commits to `Amadeus-/MuseScore` that will never go upstream, the user's normal trailer is fine — but if a fork commit may later be upstreamed or cherry-picked, prefer `Assisted-by:` from the start. Either way: **never commit/push without the user's explicit instruction** (global rule); the choice of trailer is a thing to **flag and confirm**, not decide silently. Also respect §2: the human submitter must personally understand and approve everything; an agent must not open an upstream PR/issue on its own.

## 4. Commit / PR conventions (inferred from `git log`; not formally specified in-repo)

Observed subject styles (`git log --oneline -60`):
- **Bug fixes referencing an issue:** `Fix #NNNNN: <symptom description>` (e.g. `Fix #35116: Incorrect change when halving/doubling…`, `Fix #34998: Adding an instrument unnecessarily adds numerals…`); multi-issue form `Fix #31928, #25942: …`. The subject describes the **user-visible problem**, matching the PR-template "title describes the problem" item.
- **Features/refactors/CI:** short imperative sentence, no prefix (`Add Master Volume & Pan automation`, `Move track name resolution from PlaybackController to NotationPlayback`, `Don't save ccache for pull requests`). Some contributors use lowercase/past-tense (`reworked select command to typed`, `Fixed missing includes…`, `Enabled ccache for Windows`) — style is not uniform.
- Plugin-API changes cite the issue in parentheses at the end: `Expose instrument transposition to plug-ins (#30534)`.
- **Body:** wrapped prose explaining cause and fix, then trailers (`Assisted-by:` if applicable). Merged PRs appear as GitHub `Merge pull request #NNNNN from <user>/<branch>` merge commits; framework bumps are `Updated the framework` / `Update the muse framework` (submodule pointer moves).
- **One logical change per commit**, each buildable (PR checklist `:11-12`); extensive work = a reviewable series.
- Bundle a **test** (gtest) or **vtest** reference update with behavior changes (`Update MNX test reference…`, `Add vtest for square and triangle fret diagram dots`) — see `testing-and-vtest.md`.
- Branch names upstream are contributor-chosen (`dsg-…`, `ci_ccache_fix`, `codex/fix-30534-…` visible in merge subjects); no mandated scheme found. Only `main` exists locally.
- Architectural decisions go in ADRs: `muse\docs\adr\` (template `00000-template.md`; `README.md`: numbers 1-100 reserved for past decisions; new ones start at 101+; `maintainers_ref.md` present). See `architecture-and-ioc.md`.

## 5. Code of Conduct (`CODE_OF_CONDUCT.md`)

"MuseScore Studio Code of Conduct" — **Contributor Covenant v1.4** (`:43`). Pledge: harassment-free participation. Expected: welcoming/inclusive language, respect for differing views, graceful acceptance of criticism, community focus, empathy. Unacceptable: trolling/insults/personal or political attacks, public/private harassment, publishing others' private info, sexualized language, other conduct inappropriate professionally. Maintainers may remove/edit/reject comments, commits, code, wiki edits, issues, and ban contributors (`:29`). Applies in project spaces and when representing the project (`:33`). Enforcement via the development-team contact page on the wiki (`:45`), with reporter confidentiality (`:37`).

## 6. Fork ↔ upstream relationship

- `origin` = `Amadeus-/MuseScore` (user's fork; fetch+push), `upstream` = `musescore/MuseScore` (fetch+push URLs both listed — **never push to upstream**; the user's repo is `origin`).
- Local branch `main` tracks `origin/main` (`git branch -a`). Submodules `muse\` and `muse_deps\` follow pointers recorded by the superproject (`check_submodules.yml` guards that in CI).
- The agent's standing policy: no fetch/pull/branch/submodule update/commit/push unless the user asks in the current request. Procedure for syncing the fork lives in the agent playbook `git-and-upstream-sync.md` (`~/.claude/agents/musescore/playbooks/`), not here.
- Anything contributed upstream must satisfy §1 (CLA, template), §2 (uncrustify), §3 (AI disclosure).

## Cross-subsystem dependencies

- `ci-and-release.md` — the `check_codestyle`, `check_unit_tests`, `check_visual_tests`, `check_submodules`, build workflows that gate PRs.
- `testing-and-vtest.md` — the "unit test or vtest" checklist item.
- `architecture-and-ioc.md` — ADR process/map.
- `plugin-api.md` — plugin-API commits use the `(#NNNNN)` suffix convention.
- Playbooks: `git-and-upstream-sync.md`, `add-feature-or-fix.md`.

## Non-obvious gotchas

- ⛔ `Co-authored-by:`/`Co-Authored-By:` for AI is **explicitly disallowed** upstream; use `Assisted-by:` (§3).
- ⛔ The helper scripts `cherry_pick.sh` (`:67,84`) and the CI error text in `checkcodestyle.cmake` (`:32-35`) reference `tools/codestyle/uncrustify_run_file.sh`, which **does not exist** in `tools\codestyle\` (only `cherry_pick.sh`, `globals.source`, `tidy_file.sh`). Use `tidy_file.sh <file>` instead (needs bash ≥4 and `uncrustify` on PATH). Stale reference, not a Guide error.
- `uncrustify` is not bundled; CI obtains it via `muse_deps` manifest (`uncrustify.cmake:30`). Locally you must install a compatible **0.74** (config header `:2`); other versions may format differently and fail CI.
- The cfg is K&R for control flow but Allman for functions/classes; both `nl_*_brace=remove` and `nl_fdef_brace=force` exist — don't "fix" one to match the other.
- `mod_full_brace_*=force` means uncrustify **inserts braces** around single-statement bodies — unbraced `if (x) y();` will be rewritten.
- The `.untidy` mechanism means third-party dirs are skipped by hook and CI; don't reformat them.
- Git hook (`src\` only) vs CI (`src\` + `muse\framework\`) coverage mismatch (§2.1).
- Fork Guide dir `Guide\` is currently untracked in the fork (`git status`: `?? Guide/`).
- `AI_POLICY.md` exists twice (root + `muse\`); edits to the `muse\` copy are submodule changes (different repo).

## UNCLEAR — needs verification

- Content of the wiki Contributing page and the `CodeGuidelines` wiki (naming rules, include ordering, Qt signal conventions) — external, not fetched; and which of the two CodeGuidelines URLs (`musescore/Documentation` vs `muse_framework/wiki`) is current.
- Whether `hooks\pre-commit` is installed in this clone (`.git\hooks\pre-commit` existence not checked).
- Required uncrustify minimum/exact version in the muse_deps manifest (config says 0.74; manifest not read).
- Whether upstream wants `Assisted-by:` on every commit of a series or just relevant ones (policy says "the relevant commit or commits").
- `muse\buildscripts` PR/CI details for the `muse` submodule's own repo contribution flow.

*Authored from: `CONTRIBUTING.md`, `AI_POLICY.md`, `muse\AI_POLICY.md`, `CODE_OF_CONDUCT.md`, `.github\pull_request_template.md`, `.github\workflows\check_codestyle.yml`, `muse\tools\codestyle\*`, `muse\buildscripts\ci\checkcodestyle\checkcodestyle.cmake`, `tools\codestyle\*`, `hooks\*`, `muse\docs\adr\README.md`, `git remote -v`, `git log` (2026-11-08). **If this contradicts the live source, the source wins.***
