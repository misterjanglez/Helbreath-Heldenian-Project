# Changelog Format Guide

After every `bak.py commit`, append a short summary to the bottom of `CHANGELOG.md`. One or two sentences describing what was done — no category headers, no bullet lists, no code references. Write it like a brief commit message.

The `#` header at the top of `CHANGELOG.md` should be a short summarization of all the entries — updated as needed to reflect the current contents.

## Example

```markdown
# Dialog system fixes and dead code cleanup

Fixed HudPanel right-click disabling bug and migrated right-click-close settings from dead init_defaults() into individual dialog constructors.
```

## Rules

- Short and sweet — a general summarization, not a detailed breakdown
- No file paths, struct names, or code references
- Append new entries to the bottom (oldest first, newest last)
- The `#` header summarizes the overall changelog contents
- If the log is empty, treat it as only the most recent thing done
- Before writing, present an interactive multi-select prompt listing generalized items from the session. Include an "All" option. Only include items the user selects.
