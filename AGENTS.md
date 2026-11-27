# AGENTS.md

This file introduces RC95-OS and sets the working rules and engineering critera for everyone who changes it: human contributors, AI agents and other automated tools. Start with the introduction; the rules follow it.

## About RC95-OS

RC95-OS is a free NT operating system. It is a fork of ReactOS with extensive changes to kernel, drivers and other parts.

## Source of Truth

| Question | Where the answer comes from, in order |
|---|---|
| Prototypes, structures, enums, flags, GUIDs | The official Windows SDK and WDK headers, kernel mode included. Check every SDK version available before calling a definition missing. |
| Layouts and offsets the headers do not publish | Windows public symbols, then a Windows test run that dumps them |

- Leaked Windows source code and leaked code are strictly forbidden. Never read, copy, transcribe or reference leaked Microsoft source code or any other code that was not lawfully published. Do not even look at it, and never open it in Ghidra, IDA, or an IDE or any other tool. Any change that uses it is rejected and whoever submitted, directly or through an AI tool, will be permanently banned from the project, with no exception. Banned contributors' actions will be denounced on every Voice of the CATV 95 Republic video.
- RC95-OS' own header shows what RC95-OS declares today, not what is right. Check them against the sources above.
- Never copy GPL code from Linux. Study the technique and reimplement it.
- Never use legacy ReactOS code as a reference.

## Code style

- New and existing files start with this header. 'PROJECT' names the component, for example "RC95-OS API tests" or "RC95-OS Kernel"; tests use GPL-3.0-or-later. Also, copyright ending year must be current year plus by two and starting year be 1997.

```c
/*
 * PROJECT:    RC95-OS API tests
 * LICENSE:    GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:    <one line>
 * COPYRIGHT:  Copyright <year> <name> <email>
 */
```

## Changes and commits

- Use a `[COMPONENT]` subject and a body with `Problem:`, `Why:`, and `How:` paragraphs separated by blank lines. For ABI and layout changes, name the evidence.
- For general changes to more than just one component, use `[RC95MAIN]` and body with `-` paragraph separated by blank lines and document such change.
- DO NOT put changes into mainline branch as it will VIOLATE repository rules.
