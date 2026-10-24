# Development documentation

Start with the question, not the date of a lab entry:

| Need | Authoritative location |
| --- | --- |
| Build, test or operate development tools | [Current guides](#current-guides) |
| Understand an unresolved failure or qualification gap | [Issue register](issues/README.md) |
| Find a method, observation, failed control or evidence artifact | [Experiment records](experiments/README.md) |
| Understand a completed change or an earlier milestone decision | [Implementation history](history/README.md) |
| Read current firmware/chip interfaces and behavior | [Reference documentation](../doc/README.md) |

## Current guides

- [Development and offline checks](guides/DEVELOPMENT.md): prerequisites,
  local commands, dependency boundaries and documentation review.
- [Hardware trial protocol](guides/HARDWARE_TRIALS.md): authorization,
  independent evidence, controls, preservation and restoration.
- [Host tools and report interpretation](guides/HOST_TOOLS.md): collector
  usage, private credentials and evidence-field meanings.
- [Diagnostic profile navigation](guides/DIAGNOSTIC_PROFILES.md): links from
  control families to their canonical experiments, not duplicate results.

## Current work

The [issue register](issues/README.md) owns current problem descriptions and
links to the evidence supporting them. The [functional roadmap](../../todo.md)
owns milestone checklists and acceptance criteria; the
[cleanup roadmap](../cleanup.md) owns structural cleanup tasks.
These have different roles: do not copy checklists into experiment reports
or maintain competing "remaining work" lists.

## Records and consistency

- Experiment records retain tested revisions, conditions, trial denominators,
  failures, exclusions, rejected hypotheses, artifacts and restoration.
  A later success does not erase an earlier failed control.
- Implementation history preserves old decisions and qualification boundaries.
  Its "current", "now" and "remaining" statements are local to the recorded
  build/date. Historical criteria are not active TODO checkboxes.
- Put reusable procedures in guides and link to them. In a record, retain
  the actual commands and safeguards used when they matter to provenance.
- Keep results in their canonical record. Current issue summaries and profile
  navigation link to it instead of retelling the campaign.
- When evidence changes a conclusion, update the issue register and link the
  new record. Do not silently rewrite an old observation into a new result.

The [C01 baseline](c01-baseline.json) remains byte-identical at its existing
path for tool compatibility. Other artifact paths in records describe their
original campaigns; they are not promises that those local files exist today.

All checks remain local and dependency-free. No npm packages, framework,
CI service, firmware changes or hardware access are needed for this layout.
