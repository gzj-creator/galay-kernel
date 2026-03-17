# README Awaitable Entry Implementation Plan

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Add a concise Awaitable entry section to `README.md` that points new users to E10 for state-machine awaitables and E11 for Builder protocol flows.

**Architecture:** Keep the change localized to `README.md` and add only a short selection guide. Reuse the terminology already stabilized in `docs/03-使用指南.md` so the README stays aligned without duplicating the full explanation.

**Tech Stack:** Markdown, existing examples, existing README structure

---

### Task 1: Add the compact README section

**Files:**
- Modify: `README.md`
- Inspect: `examples/include/E10-custom_awaitable.cc`
- Inspect: `examples/include/E11-builder_protocol.cc`
- Inspect: `docs/03-使用指南.md`

**Step 1: Insert the new section**

Add a compact section titled:

```md
Awaitable 入门
```

Include:

- one line for `E10-custom_awaitable`
- one line for `E11-builder_protocol`
- a short "when to choose which" summary

**Step 2: Keep the section compact**

Do not paste example code into the README. Use source file references only.

### Task 2: Verify the README update

**Files:**
- Inspect: `README.md`

**Step 1: Run grep verification**

Run:

```bash
rg -n "E10-custom_awaitable|E11-builder_protocol|Awaitable 入门" README.md
```

Expected: all anchors are found.

**Step 2: Check formatting**

Run:

```bash
git diff --check -- README.md
```

Expected: no formatting issues.
