# Backup Tool

This context describes the personal file-recovery concepts used by the Windows backup application.

## Language

**Source**:
A user-selected file or folder whose eligible state is authoritative for backup operations.
_Avoid_: Watched item, backup folder

**Destination**:
A user-configured storage location that receives Mirrors, Snapshots, or both.
_Avoid_: Target, backup drive

**Mirror**:
The latest eligible state of a Source replicated at a Destination, including source-side deletions.
_Avoid_: Backup, live backup, replica

**Snapshot**:
An immutable, dated capture of eligible source state retained separately from a Mirror to recover earlier versions.
_Avoid_: Zip backup, archive

**Backup Route**:
The configured relationship from one Source to one Destination, with Mirror and Snapshot behavior enabled independently.
_Avoid_: Pairing, job, mapping

**Eligible Item**:
A file or folder within a Source that remains after all source-type rules and exclusions are applied.
_Avoid_: Included file, watched file

**Projects Source**:
A Source whose root contains the opt-in marker and whose eligible state excludes Git repository trees.
_Avoid_: Projects folder, code backup

**Reconciliation**:
A comparison that determines and applies the work needed to make a Backup Route's Mirror match its Source.
_Avoid_: Sync, queued changes
