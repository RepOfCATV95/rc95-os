# LZMS token and repeat-history semantics

## Evidence and confidence

This document records behavior independently cross-checked against the public
wimlib implementation by Eric Biggers:

- `src/lzms_decompress.c`
  https://github.com/ebiggers/wimlib/blob/master/src/lzms_decompress.c
- `include/wimlib/lzms_constants.h`
  https://github.com/ebiggers/wimlib/blob/master/include/wimlib/lzms_constants.h
- `include/wimlib/lzms_common.h`
  https://github.com/ebiggers/wimlib/blob/master/include/wimlib/lzms_common.h

wimlib explicitly says that LZMS is undocumented and that its decoder may not
handle every possible case. These are therefore independently reproducible
implementation semantics, not a claim that Microsoft has published a normative
LZMS specification. RC95-OS uses an independently written implementation; no
wimlib source code is incorporated here.

## Item classes

The item class used for repeat-history delay is:

| Item | Class |
| --- | ---: |
| Literal | 0 |
| Ordinary LZ match, including a repeated LZ match | 1 |
| Delta match, including a repeated delta match | 2 |

The class of the immediately preceding decoded item controls which history
slot a repeat index addresses. This is not the public token enum value: the
two repeated-match token forms retain the class of their corresponding match.

## LZ repeat queue

There are three encoded repeat choices but four stored queue entries. Initialize
the queue to:

`[1, 2, 3, 4]`

The fourth entry is overflow state. For an explicit LZ offset `d`, shift
entries 0 through 2 one place right (discarding the old fourth entry), then
store `d` in entry 0.

For a repeat rank `r` in `{0,1,2}`, calculate:

`slot = r + (previous_item_class & 1)`

Take the distance from that slot, remove that entry by shifting all preceding
entries one place right, and put the selected distance in slot 0. In
particular, immediately after an LZ match, the repeat ranks address slots
1, 2, and 3 rather than slots 0, 1, and 2. This is the delayed-insertion
quirk: the newest LZ source is not selectable as a repeat on the immediately
following item.

## Delta repeat queue

A delta repeat entry is a pair `(power, raw_distance)`. Initialize four
entries to:

`[(0,1), (0,2), (0,3), (0,4)]`

Explicit delta matches insert their pair using the same four-entry shift rule
as explicit LZ offsets. A repeat rank `r` uses:

`slot = r + (previous_item_class >> 1)`

Thus, after a delta match, the three repeat ranks address slots 1, 2, and 3.
The pair is promoted to slot 0 after selection. The LZ and delta queues are
independent, but both use the same previous-item class.

## Delta match reconstruction

The power is restricted to 0 through 7. Define:

- `span = 1 << power`
- `offset = raw_distance << power`

The offset is a byte distance; it is a multiple of span. At the current output
position `p`, each byte is reconstructed sequentially as:

`output[p] = source[p - offset] + output[p - span] - source[p - offset - span]`

All arithmetic is modulo 256. The source terms and newly produced output may
overlap, so the decoder must process one byte at a time. Before decoding, the
history must contain at least `offset + span` bytes. The raw distance must be
nonzero, and arithmetic used to form offset must not overflow.

## Regression vectors in RC95-OS

`modules/rostests/unittests/libwim/lzms_token.c` exercises:

- Initial four-entry queues and the initial delta powers.
- Explicit LZ insertion, delayed lookup, and all three repeat ranks.
- Independent delta-pair history, previous-delta delayed lookup, and all three delta repeat ranks.
- A delta reconstruction vector with `power=1`, `raw_distance=2`,
  `span=2`, and `offset=4`.
- Rejection of an overflowing raw delta offset without changing history.

These tests verify token execution and history transitions. They do not by
themselves validate the entropy-coded token parser, Huffman bitstream, or
complete WIM LZMS block decoding.


## Backward Huffman/verbatim bitstream and adaptive alphabets

The Huffman/verbatim reader consumes little-endian 16-bit words from the end
of the compressed payload toward its beginning. Within each fetched word,
bits are consumed most-significant first. The bounded reader rejects odd-sized
payloads and reports input exhaustion rather than reading beyond the buffer.

Five independently adaptive alphabets are represented:

| Alphabet | Symbols | Rebuild interval |
| --- | ---: | ---: |
| Literal | 256 | 1024 symbols |
| LZ offset slot | Up to 799 | 1024 symbols |
| Match length | 54 | 512 symbols |
| Delta offset slot | Up to 799 | 1024 symbols |
| Delta power | 8 | 512 symbols |

Each alphabet begins with frequency 1 for every symbol. A decoded symbol
increments its frequency. At the rebuild interval, the current frequencies
are converted into a deterministic minimum-weight Huffman tree, its code
lengths are canonicalized (maximum 15 bits), and frequencies are diluted with
`(frequency >> 1) + 1`. The rebuild countdown then restarts.

The public wimlib implementation independently documents these alphabet
sizes, rebuild intervals, backward 16-bit word convention, and frequency
dilution behavior in `src/lzms_decompress.c`, `src/lzms_common.c`, and
`include/wimlib/lzms_constants.h`. LZMS remains undocumented, so this is a
reproducible implementation reference rather than a normative Microsoft
specification. The RC95 implementation is independently authored.

Rostest coverage checks reverse word/bit order, initialization of all five
alphabets, and the 1024-symbol literal rebuild/dilution boundary. This does
not yet constitute full entropy-coded LZMS block validation.


## Raw-block entropy grammar and framing

The raw block parser is implemented by `LzmsDecodeRawBlock()` in
`lzms_block.c`. It consumes two independent streams over the same even-sized
payload:

1. The forward stream begins with two little-endian 16-bit words forming the
   32-bit range-code register. Context-selected range decisions classify each
   item as literal, LZ match, or delta match; match decisions then select
   explicit versus repeat source and, for repeats, one of three repeat ranks.
2. The reverse stream begins at the payload end. It decodes adaptive canonical
   Huffman symbols and the verbatim extra bits for literal bytes, LZ offsets,
   match lengths, delta offsets, and delta powers.

The offset and length slot base/extra-bit tables are in `lzms_slots.c`.
Offset alphabet size is selected from the externally supplied uncompressed
block size. The decoder stops after exactly that many output bytes and rejects
odd/short payloads, invalid symbols, invalid distances, output overruns, and
overlap between the forward and reverse input cursors.

**Framing boundary:** raw LZMS does not contain its own uncompressed-size
header. In a WIM, the resource/chunk metadata supplies the compressed and
uncompressed chunk sizes; the caller passes the compressed chunk bytes and
expected output size to `LzmsDecodeRawBlock()`. This function is a raw-block
decoder, not a replacement for WIM resource-table/chunk-table parsing.

The parser currently uses the RC95 adaptive Huffman builder and context
primitives. Real-world compressed-block conformance and the exact LZMS
frequency-tree tie behavior still require verification against independently
sourced compressed vectors before the LZMS provider can be enabled.
