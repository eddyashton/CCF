---
name: "Merge {{ pullRequest.id }} to LTS branch"
labels: lts_merge
assignees: "{{ pullRequest.owner }}"
---

PR {{ pullRequest.id }} has the `for_lts` label, so should be merged to the LTS branch.
