# F2FS — native filesystem design

## Format scope

Flash-Friendly File System. This is a design contract, not a claim of implemented capabilities.

## On-disk structures and ownership

Superblock, checkpoint packs, segment information table (SIT), node address table (NAT), summary blocks, inode/node addressing and inline data. The single native C/C++ engine owns these format semantics; consumers provide bounded Common V2 host services, never competing parsers.

## Integrity and mutation invariants

Validate checkpoint version/checksum and NAT/SIT consistency. Reject uncertain roll-forward state and unsupported compression, encryption or zoned features. Garbage collection and checkpoint publication require their own recovery proof. All arithmetic, offsets and lengths are checked before I/O. Writable operations require exclusive access, explicit ordering, flush and interruption/recovery evidence. Read-only operations leave the medium unchanged.

## Common V2 boundary

Only `infiltrator_filesystem_api_v2` is public. Target the genuine PROBE/MOUNT/UNMOUNT/STAT/LIST/READ operations first; negotiate actual supported operation masks. VOLUME, METADATA and DEFRAG operations (including MAP, PLAN, EXECUTE, VERIFY, RECOVER) are individually qualified or refused. Test Media uses the same provider's qualified format and namespace operations. No FUSE, legacy ABI, private driver or kernel proxy. Hosted and import-free freestanding profiles share the format engine, with generic environment-specific host services.

## Qualification

Use mkfs.f2fs images with checkpoint alternatives, inline data, multi-node files and corrupted NAT/SIT addresses; test crash checkpoints before writes. Compare file bytes against an independent oracle, hash unchanged read-only images, inject short I/O, corruption and interruption; validate exact published module separately in BOOT and kernel. Use disposable media only. No new independent binary or test pass is claimed.

## Source and migration

[Central source and historical design, pinned revision](https://github.com/Infiltrator-Projects/Filesystems/tree/101026021d9088d80b7f0f472d9061f0e2c688a3/native/filesystems/f2fs). Adapt the retained implementation without erasing inherited notices or licences. New project-authored code uses GPL-3.0-or-later. See [migration status](../../../MIGRATION_STATUS.md), [philosophy](../../../docs/PHILOSOPHY.md) and [Common V2 contract](https://github.com/Infiltrator-Projects/Libraries/blob/main/docs/FILESYSTEM_COMMON_ABI_V2.md). Source migration is a separate step.
