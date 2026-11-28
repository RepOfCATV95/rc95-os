# BGFX host regression tests

These tests compile the production BMP decoder against a deliberately small
host shim. They exercise the same decoder implementation used by the kernel;
the shim only supplies kernel scalar types, pool allocation, and memory helpers.

Run from this directory:

```sh
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure
```

Coverage includes:

- BMP: 24-bit bottom-up rows and DWORD padding, 32-bit top-down rows,
  BGR-to-RGB normalization, output cleanup, invalid offsets, compressed
  input, unsupported bit depth, truncated pixel data, zero dimensions, and
  signed-height overflow cases.
- Framebuffer descriptors: valid RGB32 layouts, padded scanlines, unsupported
  24-bit/16-bit-style masks, overlapping/non-contiguous channel masks,
  invalid geometry, out-of-range offsets, and pitch/frame-size overflow.
- Rendering/ownership: RGB channel packing, rectangle and bitmap blits,
  centering, clipping/bounds rejection, framebuffer MMIO mapping alignment,
  offset application, invalid replacement teardown, and idempotent unmapping.
- Animation lifecycle: independent periodic-DPC frame advancement without
  progress callbacks, 100 ms time gating, progress/timer de-duplication,
  frame-zero presentation, wrap-ready sequencing, timer cancellation, DPC
  draining, partial-load cleanup, mismatched-frame cleanup, and undersized
  framebuffer cleanup.

The framebuffer validator is compiled directly from the production
`bgfx_framebuffer_validate.c`; rendering tests compile the production
framebuffer and rendering modules with test stubs for ARC discovery and MMIO
mapping. Animation tests compile the production `bgfx_animation.c` against a
small deterministic timer/DPC model. The host shim supplies only the ARC
payload shape and kernel scalar/helper definitions.

This is a host decoder test, not a substitute for an NTOSKRNL build or a
framebuffer/firmware integration boot test.
