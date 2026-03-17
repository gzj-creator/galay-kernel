# Awaitable Example Choice Guide Implementation Plan

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Add a concise choice guide to `docs/03-使用指南.md` that tells users when to use the E10 state-machine example versus the E11 Builder example.

**Architecture:** Keep the change documentation-only and local to `docs/03-使用指南.md`. Insert one compact section between the current implementation notes and the further-reading section, with explicit links to `E10-custom_awaitable` and `E11-builder_protocol`.

**Tech Stack:** Markdown, existing example source files, existing usage guide

---

### Task 1: Insert the choice-guide section

**Files:**
- Modify: `docs/03-使用指南.md`
- Inspect: `examples/include/E10-custom_awaitable.cc`
- Inspect: `examples/include/E11-builder_protocol.cc`

**Step 1: Add the new section**

Add a section titled:

```md
如何选择：状态机还是 Builder
```

Include:

- one-line description for `E10`
- one-line description for `E11`
- recommended selection bullets
- a tiny side-by-side usage skeleton

**Step 2: Keep it compact**

Do not paste the full examples into the guide. Link to:

- `examples/include/E10-custom_awaitable.cc`
- `examples/include/E11-builder_protocol.cc`

### Task 2: Verify the doc update

**Files:**
- Inspect: `docs/03-使用指南.md`

**Step 1: Run grep verification**

Run:

```bash
rg -n "E10-custom_awaitable|E11-builder_protocol|如何选择：状态机还是 Builder" docs/03-使用指南.md
```

Expected: all three anchors are found.

**Step 2: Check formatting**

Run:

```bash
git diff --check -- docs/03-使用指南.md
```

Expected: no whitespace or patch-format issues.
