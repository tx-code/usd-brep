---
name: usd-brep-merge-upstream
description: "Review, schedule, or merge upstream changes into the usd-brep fork, preserving SDK behavior and validating matching native consumers before a local main fast-forward. Use for upstream synchronization, not release publication or an ordinary origin pull."
---

<!-- SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved. -->
<!-- SPDX-License-Identifier: Apache-2.0 -->

# Merge upstream into this fork

Use [AGENTS.md](../../../AGENTS.md) for project direction and
[FORK_MAINTENANCE.md](../../../FORK_MAINTENANCE.md) for remote, branch, and incorporation policy.
Those files own repository policy; this skill owns the integration workflow. An upstream release does not
itself authorize a fork release, push, PR, or publication.

## Choose the boundary

Match the requested action: checking for updates ends with a reviewed delta; scheduling records a pending
integration at a concrete development boundary; an authorized merge continues through compatibility fixes
and validation. Keep the active import/viewer objective and its existing quality gates in place.

Inspect Git status, remotes, worktrees, and the branch graph in both the integration and destination
checkouts. Preserve unrelated tracked and untracked work. Reuse a suitable development worktree; create a
temporary `codex/` branch only when isolation is needed. Complete the current feature's agreed checks before
its integration boundary; do not hide its unfinished changes in the upstream merge or automatically stash them.

Fetch the requested upstream ref, then pin the local baseline, merge base, and upstream commit by full SHA.
If upstream is already an ancestor of the destination, report that fact without creating a merge. If work
must wait, keep the pinned target, pending action, and blocking feature checkpoint in the existing local work
record. A fetched ref is not an incorporated commit.

## Review the full delta

Compare upstream against the merge base and inspect overlapping fork changes. Account for affected layers,
added/deleted paths, build/runtime dependencies, and tests, including changes Git can merge automatically.
A release title or conflict count does not establish its behavioral scope.

Apply the current project direction when deciding whether upstream paths belong in the fork. In particular,
evaluate resurrected files and dependency changes against the SDK's deliberately removed integrations.
For each behavioral overlap, identify the upstream fix and the fork invariant it must preserve. Resolve the
behavior rather than selecting an entire side of a source file. Preserve upstream license/provenance notices.

Finish this review with a bounded integration plan: affected contracts, compatibility edits, relevant baseline
evidence, and the tests needed to distinguish preserved behavior from a regression. Keep per-run details local.

## Integrate with ancestry intact

Bring the temporary development branch up to date with local `main` before merging the pinned upstream SHA.
For divergent histories, use a merge that preserves upstream ancestry and inspect the resulting index before
committing. Retain intended upstream fixes together with the fork's ownership, source semantics, and publication
contracts; add focused synthetic regressions where compatibility code changes behavior.

Keep feature work and upstream integration reviewable as separate commits. If the destination advances during
validation, integrate its new commits without flattening the upstream merge through an ordinary rebase, then
revalidate the affected result. An unresolved behavioral conflict leaves integration incomplete; report the
specific contract or evidence still needed instead of silently discarding a fix.

## Validate the actual merged runtime

Select commands from the current [build guide](../../../README.md#build-it), owning component contracts, and
[test-selection skill](../usd-brep-test-selection/SKILL.md). Start with affected checks and broaden for shared
kernel, ownership, numerical, ABI, or build changes. Establish the relevant pre-merge result when needed to
attribute failures; do not repeat unrelated suites without a changed risk or unresolved failure.

- Build libraries and all affected consumers with matching C++ standard, configuration, CRT, and kernel layout.
  Verify which DLLs, extensions, worker, and CLI the tests actually load. For header/layout changes, verify
  dependency tracking or force the affected rebuild; a fast no-op build alone is insufficient evidence.
- Compare source semantics, diagnostics, independent geometry truth, ownership/cleanup, and cancellation where
  affected. Native validity and a rendered mesh establish only their own checks. Keep error budgets and explicit
  interpretation/repair permissions unchanged; investigate changed numerical results rather than blessing them
  by updating fixtures or loosening thresholds.
- Exercise packaging and GUI consumers when changed shared libraries reach them. Use a separate matching runtime
  for validation so existing delivered runtimes and user windows retain compatible binaries.

Use synthetic inputs for tracked reproductions. Private model data, identifying inventories, geometry, screenshots,
and detailed comparison records remain local under the user's confidentiality rules.

## Close the local integration

After checks pass, record the incorporated upstream SHA and compatibility notes in the canonical fork/release
records. Add a changeset only when the resulting change meets the repository's caller-visible change policy;
upstream version metadata alone does not choose a new fork version.

Commit the reviewed integration, verify the pinned upstream commit is its ancestor, and verify local `main`
can fast-forward to that exact tested result. Recheck destination changes and preserve unrelated work before
the fast-forward. Remove only obsolete, merged temporary branches or managed worktrees within the authorized
cleanup scope. Keep an incompletely validated integration on its branch with its remaining work recorded.

Report separately what was fetched, merged, validated, and advanced to local `main`; state any remaining blocker.
Remote publication follows the user's existing authorization, independently of completing the local merge.
