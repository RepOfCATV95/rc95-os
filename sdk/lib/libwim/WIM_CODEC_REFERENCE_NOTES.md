# WIM codec implementation cross-check

This note consolidates two independently maintained decoder references for the
WIM compression work in libwim. They are used to cross-check format behavior;
their implementation code is not copied into this project.

## Independent references

### XPRESS-Huffman (separate from WIM's XPRESS resource codec)

- Reference implementation: [fastvol XPRESS decoder](https://github.com/code-zm/fvol/blob/59144d12c93e9ba7ee0716dbb879f1682496d1f2/src/codecs/xpress.rs)
- Independent fixture generator: [gen_xpress.py](https://github.com/code-zm/fvol/blob/59144d12c93e9ba7ee0716dbb879f1682496d1f2/tests/fixtures/codecs/gen_xpress.py)
- Compressed fixture set:
  - [xpress_huff_small.bin](https://github.com/code-zm/fvol/blob/59144d12c93e9ba7ee0716dbb879f1682496d1f2/tests/fixtures/codecs/xpress_huff_small.bin)
  - [xpress_huff_medium.bin](https://github.com/code-zm/fvol/blob/59144d12c93e9ba7ee0716dbb879f1682496d1f2/tests/fixtures/codecs/xpress_huff_medium.bin)
  - [xpress_huff_multiblock.bin](https://github.com/code-zm/fvol/blob/59144d12c93e9ba7ee0716dbb879f1682496d1f2/tests/fixtures/codecs/xpress_huff_multiblock.bin)
  - [xpress_huff_zeros_runs.bin](https://github.com/code-zm/fvol/blob/59144d12c93e9ba7ee0716dbb879f1682496d1f2/tests/fixtures/codecs/xpress_huff_zeros_runs.bin)

The reference's fixture tests describe the four generated compressed vectors and
also identify a real Windows 10 Prefetch page (MAM\x04 / XPRESS-Huffman) in its
test corpus. The generated vectors cover single-block, medium, multi-block and
long zero-run cases. They are suitable independent compressed-input regression
fixtures; they should not be described as all being Windows-captured data.

Cross-checked XPRESS-Huffman rules:

1. Each output block begins with a 256-byte code-length table. Each byte stores
   two 4-bit lengths, low nibble first, for 512 symbols.
2. The 512-symbol canonical Huffman code has a maximum code length of 15.
   A valid table must fill the code space exactly; incomplete/oversubscribed
   tables are malformed.
3. Bits are consumed most-significant-bit first from little-endian 16-bit words.
4. A block produces at most 65,536 output bytes. The next table begins at the
   next block's input position, which must account for prefetched words and
   match-length bytes interleaved into the bitstream.
5. Symbols 0..255 are literals. Symbols 256..511 encode match length and
   offset-bit count: the low four bits select the length header and the upper
   bits select the offset slot. The offset is the decoded extra bits plus the
   slot's implicit leading power of two.
6. Length header 15 uses the extension sequence (one byte, then a 16-bit value
   when the byte is 255, then a 32-bit value when that word is zero). The
   extension bytes are read at the format-defined word boundary, not simply
   from the current Huffman bit position.
7. Backreferences must be nonzero and no greater than bytes already produced;
   overlapping forward-copy semantics are required.

Implementation note: use a checked bit reader first. A fast lookup table can be
added only after it is differential-tested against the checked decoder. Test
both output-size boundaries and truncated interleaved extension bytes.

### WIM LZX

- WIM-specific implementation: [exav WIM LZX decoder](https://github.com/sylvinus/exav/blob/66d44c17306cc1da66a9c292e6fdc94d17baca0a/crates/exav-unpack/src/formats/wim/lzx.rs)
- Independent LZX implementation used for cross-checking: [wimlib lzx_decompress.c](https://github.com/TechUnRestricted/WinDiskWriter/blob/34ab9ffce135936ae557212957e6e11df39db768/WinDiskWriter/libs/wimlib/src/lzx_decompress.c)

WIM uses the LZX coding model but not Cabinet framing. The two references agree
on the essential WIM-specific distinctions:

1. Every WIM compressed chunk is an independent LZX stream; decoder state,
   including the three recent offsets and delta-coded tree lengths, is reset
   for each chunk.
2. WIM LZX uses a 32 KiB window/chunk model. Its block header has a 3-bit type
   and a one-bit size selector: set means 32,768 bytes; clear means a 16-bit
   block size. This differs from Cabinet's flat 24-bit block-size field.
3. Bits are read MSB-first from little-endian 16-bit words.
4. Block types are verbatim (1), aligned (2), and uncompressed (3).
   Other block types must be rejected.
5. Verbatim and aligned blocks carry delta-coded main and length Huffman trees.
   A 20-symbol pretree decodes code-length deltas and zero/repeated runs.
   Aligned blocks additionally carry an eight-symbol, 3-bit aligned-offset tree.
6. The main tree covers 256 literal symbols plus match symbols grouped by
   length header and offset slot. A separate length tree extends long matches.
7. The three repeated offsets begin at 1, 1, 1. Explicit offsets update the
   recent-offset queue; repeat slots have their own promotion behavior.
8. Uncompressed blocks align to the next 16-bit word, carry three 32-bit recent
   offsets, then raw bytes; odd-sized raw blocks are padded to an even boundary.
9. WIM applies the E8 call transform unconditionally, using the WIM nominal
   file-size constant, rather than a Cabinet stream header flag. The E8 transform
   must be applied after decoding the complete chunk.

The exav implementation is a compact, readable WIM-specific cross-check. The
wimlib decoder provides a second implementation of block decoding, tree
construction, recent-offset handling and E8 postprocessing. Where they differ
in implementation strategy, the MS-WIM/MS-XCA format rules remain authoritative.

## Required libwim integration and validation

- Keep XPRESS-Huffman and LZX as separate providers and separate decoder state.
- XPRESS-Huffman is not the XPRESS resource algorithm selected by WIM header
  compression flags. Do not advertise `WIM_CODEC_CAP_WIM_CHUNKS` for it or route
  WIM XPRESS resources to it. Its provider advertises decode only; encoding and
  streaming remain unsupported.
- Decode exactly the caller-supplied expected output size, enforce the maximum
  output size before every write, and reject invalid trees, block sizes,
  offsets, lengths, truncated words, and non-progressing blocks.
- Add the four XPRESS-Huffman reference-encoder vectors above as binary test
  fixtures (or byte-for-byte preserved test resources) with expected output
  generated by the matching fixture generator. Keep a distinct test labelled
  as real Windows-captured data only when the actual captured compressed bytes
  and expected decompressed bytes are imported with provenance.
- Add LZX vectors covering all three block types, aligned offsets, repeated
  offsets, multi-block chunks, E8 translation, truncated streams and bad
  backreferences. Compare output to an independent decoder and verify WIM
  resource SHA-1 values where a full WIM fixture is available.
- Run the dedicated libwim unit tests and the repository build workflow before
  claiming conformance.

## Current implementation status

`wim_xpress_huffman.c` now contains a checked, one-shot XPRESS-Huffman block
decoder. `libwim_xpress_huffman_unittest` checks a 3000-byte reference-generated
vector and a 4088-byte Windows Prefetch compressed page. The implementation is
an initial milestone,
not yet validated by a completed repository build. It does not implement
streaming or encoding and is not wired into WIM XPRESS resource dispatch.
`wim_lzx.c` now contains an initial bounded WIM-specific 32 KiB chunk decoder
with verbatim, aligned, and uncompressed block handling, per-chunk tree and
recent-offset reset, and E8 postprocessing. `libwim_lzx_unittest` covers a hand-built uncompressed-block chunk, truncation,
a hand-encoded verbatim block with complete canonical trees, a two-block
uncompressed chunk, and E8 address translation. The aligned-offset path and
independent compressed-vector differential comparison remain outstanding.
The latest repository workflow compiled `wim_lzx.c` and linked the LZX test
executable, but the overall build failed in unrelated Btrfs and hibernation
sources (`DEV_ITEM`/`device` member mismatches, `BOOL`,
`PO_BTRFS_DEVICE_ENTRY`, and `MAXULONGLONG`); therefore this is not a passing
repository build or a conformance claim.
