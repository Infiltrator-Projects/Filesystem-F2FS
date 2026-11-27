# F2FS: technical reference

## History and design objectives

Samsung introduced F2FS in 2012; it entered Linux 3.8 in 2013. It is a log-structured filesystem designed around flash translation layers and erase-before-write media. Updating a file normally writes new blocks rather than overwriting existing blocks. Stable node IDs avoid recursively changing every ancestor when a node moves: a Node Address Table (NAT) translates IDs to current physical block addresses. A Segment Information Table (SIT) tracks live blocks. Segment summaries identify their owners. Checkpoint packs publish coherent state. These four mechanisms must be understood together; a valid block address alone does not establish ownership.

## Media geometry and superblock

This chapter uses the conventional 4096-byte block, 512-block segment profile in Linux v6.12. Segment size is 2 MiB. A section groups segments, a zone groups sections. The primary and duplicate superblocks are at byte 1024 and 4096+1024, respectively. Numeric fields are little-endian. Relative to either record: magic 0xF2F52010 at 0; major/minor version at 4/6; log2 sector size at 8; log2 sectors per block at 12; log2 block size at 16; log2 blocks per segment at 20; segments per section at 24; sections per zone at 28; checksum offset at 32; 64-bit block count at 36. Validate each copy independently; verify checksum when required, geometry and feature support before choosing one.

## Superblock area boundaries

Relative offsets 44,48,52,56,60,64,68 hold section count, segment count, and segment counts for CP, SIT, NAT, SSA and Main. Block addresses at 72,76,80,84,88,92 name segment zero and starts of CP, SIT, NAT, SSA and Main; root, node and metadata inode IDs at 96,100,104; UUID at 108 and UTF-16LE volume name at 124 (512 code units). Feature word at 2180; CP payload size at 1664. Validate the declared ranges against each other and the volume, using checked products. CP is segment-aligned; SIT/NAT have shadow copies; SSA holds summaries for Main segments. Main is the sole normal node/data allocation area. Multi-device/zoned layouts need separate geometry validation.

## Checkpoint record layout

The two CP packs start at cp_blkaddr and cp_blkaddr+blocks_per_segment. In the 4 KiB reference profile, relative offsets in a checkpoint header: version 0, user block count 8, valid block count 16 (all 64-bit); reserved, overprovision and free segment counts at 24,28,32; eight node segment numbers at 36 and eight 16-bit offsets at 68; data segment numbers at 84 and offsets at 116. Flags 132, pack length 136, summary start 140, valid node/inode counts 144/148, next free NID 152, SIT/NAT version bitmap sizes 156/160, checksum offset 164, elapsed time 168, allocation-type bytes 176..191, bitmap area from 192. Flags can change bitmap placement, payload and summary encoding. Bound all fields and verify first and last checkpoint blocks and versions before selecting the newer complete pack; account for version wrap.

Inode checksums, inode creation time and recovery/checkpoint feature flags affect admission. The checkpoint, segment and node code defines persistence; Linux cache, crypto and workqueue structures are not disk records. Test two conflicting checkpoint packs, corrupted NAT/SIT addresses and garbage collection independently.

## Node Address Table and shadow selection

A packed NAT entry is nine bytes: one-byte generation/version at 0, owner inode at 1, physical node block at 5. There are floor(4096/9)=455 entries per block. For NID 1000, table block index is 2, slot 90 and byte offset 810. For B blocks per segment and table block index t, the first shadow copy is at nat_blkaddr+floor(t/B)*2B+(t mod B); the alternate is B blocks later. Use the selected checkpoint's NAT version bitmap and override entries with the valid checkpoint NAT journal. NIDs are logical identifiers, not physical addresses. Check node block address against Main, verify the node footer and owner, and distinguish zero/unallocated and reserved sentinel addresses.

## Segment Information Table

Each packed SIT entry occupies 74 bytes: 16-bit vblocks at 0, 64-byte validity bitmap at 2, 64-bit modification time at 66. Low ten vblocks bits hold the live-block count; upper six identify allocation type. A 4 KiB table block contains 55 entries. Segment s maps to entry s mod 55 in table block floor(s/55). SIT shadow copies occupy halves of the SIT area; use the checkpoint bitmap to select each block and overlay the SIT journal. The validity map uses F2FS's MSB-first convention; count set bits and reconcile with the declared valid count. A block's physical segment and index are derived from its offset from main_blkaddr. A clear bit does not permit immediate reuse if the previous committed checkpoint still references it.

## Segment summaries and reverse ownership

A 4 KiB segment summary contains 512 seven-byte entries (3584 bytes), a 507-byte journal area and five-byte footer. Entry fields: NID at 0, version at 4, 16-bit offset-in-node at 5. For data, the NID identifies the node containing the owning data pointer; for a node block it identifies the node itself. Footer type distinguishes node and data summaries. For closed Main segment s, SSA block is ssa_blkaddr+s; current segments can instead use CP summaries or recovery reconstruction. The NAT journal has a two-byte entry count followed by 13-byte records (NID plus NAT entry); the SIT journal has a count and 78-byte records (segment number plus SIT entry). Validate journal capacity and duplicate-key rules. A cleaner must confirm reverse ownership against the current forward mapping, not just trust a stale summary.

## Inode fields and node footer

The baseline node is 4096 bytes. Its 24-byte footer begins at 4072: NID 0, owner inode 4, flag 8, checkpoint version 12 (64-bit), next node block 20. Inode offsets: mode 0, advise 2, inline flags 3, UID 4, GID 8, link count 12, size 16, allocated-block accounting 24, access/change/modify seconds 32/40/48; nanoseconds 56/60/64, generation 68, directory depth 72, xattr NID 76, flags 80, parent inode 84, name length 88, name 92..346, directory level 347, cached extent 348..359. The mapping union starts at 360. Five child NIDs start at 4052. Extra attributes and inline xattrs reduce usable inode data-pointer capacity; do not confuse inode inline flags with filesystem feature flags. Cached extent is an optimisation, not independent ownership evidence.

## File mapping and a worked example

Without extra or inline reservations, the inode holds 923 direct data addresses; direct nodes contain 1018 physical addresses; indirect nodes contain 1018 NIDs. Five inode child NIDs represent two direct, two indirect and one double-indirect subtree. Logical blocks 0..922 use inode entries; 923..1940 use the first direct node; 1941..2958 use the second. Logical block 3000 therefore uses the first indirect node's child 0, data slot 41. Resolve both child NIDs through NAT, checking their footers, then read the resulting physical data address. At 4096 bytes per block, physical block 12345 begins at byte 50565120. Clip to EOF. Zero is an absent address; 0xffffffff and 0xfffffffe have reserved/new and compression-marker meanings, respectively, and must not be interpreted as physical locations.

## Directories and hashing

A conventional 4096-byte directory block holds 214 slots. The allocation bitmap occupies bytes 0..26, reserved bytes 27..29, 214 packed eleven-byte records begin at 30 and 214 eight-byte name fragments begin at 2384. Each record: hash 0, inode 4, name length 8, type 10. A name of length L consumes ceil(L/8) consecutive slots; a 17-byte name requires three. Only its first record is the entry header. The native bitmap marks all occupied slots. F2FS uses a multilevel hash-bucket layout: bucket count grows with level, directory-level parameter and a capped exponent; each bucket spans two or four blocks according to the level. Use the original hash (TEA-derived for ordinary names) and exact level rules from dir.c/hash.c; encrypted/casefold names require their own policies. Hash matches still require name comparison. Inline directories use a different geometry derived from the inode's available inline space.

## Allocation and cleaning

The ordinary six active logs separate hot/warm/cold node traffic and hot/warm/cold data traffic. New writes can be placed sequentially (LFS) or into suitable invalid slots (selective segment reuse). Track validity in SIT and reverse ownership in summaries; invalidate superseded locations only under checkpoint-safe rules. Garbage collection selects victims using valid-block cost and, in some modes, age; it moves live blocks to other segments and retires a segment only after no committed references remain. Moving a data block requires updating its owning node; moving a node updates NAT instead. Example: a victim with 100 live blocks out of 512 can yield 412 net free blocks after copying 100, ignoring metadata and reserved-space overhead. Pinned files and zoned-media restrictions can prevent relocation.

## Checkpoint publication and roll-forward

A writer must persist payload and node blocks, flush or stage NAT/SIT changes to alternate copies or checkpoint journals, write new checkpoint content and publish the terminating checkpoint block with matching version and checksum. Never overwrite a still-needed shadow version before the old checkpoint ceases to be authoritative. Mount recovery first selects a complete valid checkpoint, then processes orphan lists and eligible fsync-marked node chains where the revision and flags require roll-forward. The footer's checkpoint version and next address are essential to that scan. Validate node identity, bounds, transaction membership and termination before applying recovered ownership. Roll-forward is not a generic redo journal or permission to replay arbitrary later-looking blocks. Read-only inspection must not replay onto the source.

## Additional feature families and format

Extra inode fields include project IDs, inode checksums, creation time and compression parameters. Inline data and xattrs share inode space and can reduce mapping capacity. Compressed files encode a cluster marker followed by a bounded sequence of compressed blocks; the decompressor and checksum policy are feature-specific. Encryption, verity, casefold, quotas, zoned allocation and multi-device mappings each impose independent requirements. A formatter must lay out both superblocks, the two CP packs, shadow NAT/SIT, SSA and Main, initialise reserved NIDs, root namespace and summaries, and publish valid checkpoint state last. It must never simply emit a signature. The independent implementation must refuse unsupported writable feature combinations.

## Implementation and qualification boundary

The independent provider uses Common V2: https://github.com/Infiltrator-Projects/Libraries/blob/main/docs/FILESYSTEM_COMMON_ABI_V2.md . Negotiate actual operation masks and media profiles. Distinguish unsupported, corrupt, I/O and stale-handle results. Require bounded host I/O, exclusive unmounted ownership and durable flush before mutation. Test Media and Defragmenter consume the same engine, not separate format parsers. Hosted and freestanding binaries, relocation, recovery, BOOT, kernel integration and packages require separate exact-source evidence. Current support belongs to the repository README and qualification records. The intended package paths are /System/Filesystems/ and /System/Resources/Documentation/; the reference is not an installation or release-status claim.

## Verification and references

Cross-check the exact packed structures against Linux v6.12 include/linux/f2fs_fs.h: https://github.com/torvalds/linux/blob/v6.12/include/linux/f2fs_fs.h . Design documentation: https://docs.kernel.org/6.6/filesystems/f2fs.html . Reference algorithms: fs/f2fs/super.c, node.c, segment.c, checkpoint.c, recovery.c, dir.c and hash.c in the same kernel tree. Independent tools: https://git.kernel.org/pub/scm/linux/kernel/git/jaegeuk/f2fs-tools.git/ . Test both checkpoint copies, shadow bits and journal overrides, root and node identity, direct/indirect transitions, long names, sparse and compressed files, invalid summary ownership, orphan processing, interrupted GC and checkpoint publication. Use independent mkfs/fsck/dump images as oracles, not production fallbacks. Exact checksum coverage, large NAT bitmap layouts, compact summaries, hash implementation and feature-dependent recovery still require dedicated specification and fixture verification before claiming full independent write compatibility.

Cross-check these implementation sources with the native on-disk definitions.

Retained source: https://github.com/Infiltrator-Projects/Filesystems/blob/101026021d9088d80b7f0f472d9061f0e2c688a3/native/filesystems/f2fs/linux/f2fs.h (blob e1cbd3917b).
