---
name: "Merge {{ pullRequest.pull_number }} to LTS branch"
labels: lts_merge
assignees: "{{ pullRequest.owner }}"
---

PR {{ pullRequest.pull_number }} has the `for_lts` label, so should be merged to the LTS branch.
