# Gnarlyr website UX pass

Completed 2026-11-10 with ds-ux and ds-consult. Retained round 6, source commit
`291b2fd`, and published through the existing DoublePaaS static-site workflow.
Firmware and unrelated work were unchanged by this website pass.

## Outcome

Preparation precedes copyable build and checksum commands. The commands remain
fully readable on phones, with prompts excluded from copied text. Installation
distinguishes first installation from updating Gnarlyr, and recovery has its own
navigation destination. Touch and keyboard clipboard-denial instructions reflect
the input used. The cassette identity, feature details and existing source-build
journey are preserved; no release URL or firmware screenshots were invented.

## Council scores

Each round had fresh independent Codex and Claude evaluators. Combined cells use
the lower rating; device totals average seven cells. The aggregate is 70% of the
three-device mean plus 30% of the lowest device. Ratings describe the inspected
journey and states.

| Round | Writer | Mobile | Tablet | Desktop | Aggregate | Paired gain | Decision | Retained best |
| --- | --- | ---: | ---: | ---: | ---: | ---: | --- | --- |
| Baseline | — | 6.6 | 6.7 | 6.9 | 6.7 | — | Baseline | Baseline |
| 1 | Codex | 7.1 | 7.3 | 7.4 | 7.2 | +0.500 | Accept | 1 |
| 2 | Claude | 7.6 | 7.6 | 7.7 | 7.6 | +0.433 | Accept | 2 |
| 3 | Codex | 7.7 | 7.9 | 7.8 | 7.8 | +0.160 | Accept | 3 |
| 4 | Claude | 7.9 | 8.0 | 7.9 | 7.9 | +0.110 | Reject | 3 |
| 5 | Codex | 7.9 | 8.0 | 7.9 | 7.9 | +0.126 | Accept | 5 |
| 6 | Claude | 8.1 | 8.1 | 8.0 | 8.0 | +0.126 | Accept | 6 |

Round 4 stretched the terminal to match expanded preparation. Codex scored a
desktop spacing regression because a large gap separated the checksum command
from its output. The acceptance rule rejected it despite a higher combined
aggregate. Round 5 kept the terminal compact and used bounded desktop stickiness.
Paired gains compare with the retained best, so round 5 compares with round 3.
An arithmetic error in the round-2 written summary was corrected from its
unchanged matrices; it was not scored as progress.

| Final category | Mobile | Tablet | Desktop |
| --- | ---: | ---: | ---: |
| Typography | 8 | 8 | 7.5 |
| Hierarchy | 8 | 8.5 | 8.5 |
| Spacing & density | 8 | 8 | 8.5 |
| Color & contrast | 8 | 8 | 8 |
| Iconography | 8 | 8 | 8 |
| Consistency & polish | 8 | 8 | 7.5 |
| Clarity | 8.5 | 8 | 8 |

The aggregate improved from 6.688095 to 8.033333. Both reviewers accepted the
retained final candidate without a category regression. The pass stopped at the
six-round budget; the default target of every device reaching 9 with no cell
below 8 was not reached. Desktop typography and polish remain the lowest cells.

## Checks and follow-ups

Inspected 390×844, 768×1024 and 1440×900 journeys, plus 360px stress views,
copy feedback, keyboard focus and expanded preparation. No page or command
overflow was detected. At 360px, setup fits one line and the checksum takes three
lines at 13px. Keyboard focus stays visible across the 699/700px height and
1000/1001px width thresholds.

Exact command strings, selection newline, native clipboard completion, pending
state, feedback resets, hybrid input handling, deep-link reopening, unique IDs,
anchors and status bindings passed their covering checks. JavaScript syntax and
publication validation passed. New sequence numbers measure 8.44:1 against the
background. Parser and firmware tests were not run because their source was not
changed by this task.

Actual 200% browser zoom, hardware touch selection menus and spoken screen-reader
output remain unevaluated. Further refinements could align the desktop environment
description columns and repeat “before ejecting” beside the copy check. Authentic
firmware screenshots can be added when available.

The live four-file readback, health, private-path exclusions, origin TLS and
keyboard-copy checks passed. See [deployment and rollback](deployment.md).
Disposable review captures were removed after the comparison; accepted source
and this summary remain in Git.
