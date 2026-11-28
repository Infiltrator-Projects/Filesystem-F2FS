# Filesystem-F2FS

Native Common V2 offline reader development. Recognition verifies redundant superblock geometry and complete checkpoint-pack CRCs. The new reader selects the newest complete clean checkpoint, uses native NAT/SIT shadow bitmaps and checkpoint journals, reconstructs live allocation counts, validates node identity and provides bounded stat/list/read through ordinary and inline directories and direct data mappings. Initial mount scope excludes feature extensions, multi-device/zoned layouts, checkpoint payloads, dirty recovery, external xattrs and higher indirection. The published PDF-only release remains unchanged pending exact-source namespace and independently populated-media qualification. No write, format, repair, defragmentation or physical BOOT/kernel qualification is claimed.

[Manual](docs/F2FS.pdf) · [Technical reference](docs/F2FS-TECHNICAL-REFERENCE.md) · [Migration status](MIGRATION_STATUS.md) · [Common V2](https://github.com/Infiltrator-Projects/Libraries)
