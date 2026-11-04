---
name: HackRF scope
description: The user's intended hardware role for HackRF and the current implementation boundary.
---

The user intends HackRF to handle RF transmission in the eventual design. The current implementation is display-only and does not include an RF bridge or HackRF control.

**Why:** The user stated this intended hardware arrangement, while the current approved work is limited to the display port.

**How to apply:** Keep this architecture context in mind, but do not treat the future goal as permission to implement RF transmission or bridging.
