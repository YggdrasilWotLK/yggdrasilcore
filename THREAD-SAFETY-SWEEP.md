# Thread-safety sweep (PR #60, follow-up to #57/#58/#59)

Crash class: cross-thread data races and use-after-free on shared game
state (map player lists, group/BG/LFG membership, template/spell/event
stores). Symptom: intermittent segfaults on logout/teleport/instance
reset/BG end/reload under load.

## What changed, by area

- Map: `CopyPlayerGuids()`/`GetPlayerCountLocked()` snapshot helpers;
  `HavePlayers()` locked; `DoForAllPlayers`, encounter/sound/zone loops,
  loot-recipient and transport updates snapshot + re-resolve with
  `IsInWorld`/`GetMap` checks; dead `m_mapRefIter` removed;
  `MapMgr::FindBaseMap` and corpse-grid accessor locked; updatable-list
  recheck is GUID-snapshot; grid-visit non-mutation contract documented;
  creature/GO/dynamic/pet removal unlinks stores before callbacks.
- Scripts (~150 sites, core + all expansions): live `GetPlayers()` /
  `GetFirstMember()` loops converted to snapshots (teleport-in-loop,
  `getFirst`, raid-difficulty and Vault-Archavon paths included).
- Social: `Group::GetLeaderName` returns `std::string`;
  `GetMembersCount`/`GetInviteeCount`/`IsDisbanded`/`GetTargetIcon`
  lock internally; new `GetMemberGUIDs()`; LFG selected/locked dungeons,
  players and comments return by value; BG `GetPlayerGUIDs()` /
  `GetPlayersSnapshot()` / `HasPlayer` / `GetSpectatorCount`; BG
  broadcasts/resurrect/safe-position and LFG expiry/invite flows are
  snapshot-then-act; `BG::Update`/`Mgr::Update` outer locks removed
  (atomic tick state); `ArenaSeasonMgr` queue-copy bug fixed;
  `ArenaTeam::GetMembersSnapshot`; guild broadcast/count/log snapshot;
  instance reset-time accessors locked, bound instances by value;
  `DestroyGroup` clears offline/invite/BG-raid holders; channel member
  count/bans/rights locked, `plrPtr` is now a GUID.
- Stores: WhoList swap-publish + snapshot; `getStringConfig` returns
  `std::string`; `DisableMgr::AddDisable` locked; `ConditionMgr`
  `_condLock` + deferred delete across reloads; achievement getters
  copy-out + `_achLock`; `GameEventMgr` recursive guard on all methods,
  guid maps privatized; WorldState transport/SI/Quel'Danas state locked;
  `ObjectAccessor` rename under one lock; session map `_sessionsLock` +
  `SnapshotSessions()`; `SpellMgr`/`ObjectMgr` template getters and
  reloads under recursive locks with `ForEach*` iteration migrated.
- Lifetime: BG queue invite/remove events revalidate; spell far-target
  and RAF-summon validate; chase null-target guard; SmartScript tick
  aborts on dead base object and captures GUIDs in delayed events;
  `i_objectsToRemove` invalidated on alternate delete paths.
- Lock order (outer to inner): world/session > social > guild/channel >
  map > leaf stores. Snapshot-then-act; never hold across I/O.

## Module impact

No adaptation required to build: all changed APIs are source-
compatible (`const&` to by-value binds to existing `const&` locals;
`const char*` to `std::string` converts at use sites).
`mod-shadows` and `mod-yla` still contain `GetFirstMember()` loops
(bot AI, raid triggers, Lua group methods) — same race, needs its own
migration pass. `mod-anticheat` and `mod-breaking-news-override`
are clean.

## Performance

Net roughly neutral. Added cost: one recursive-mutex round-trip per
spell/template lookup (hottest path; critical sections are map finds
only) and per-getter shared locks on loot/gossip/kill-credit paths,
plus small reserved vector snapshots per broadcast/tick. Removed cost:
`BG::Update`/`Mgr::Update` no longer serialize all battlegrounds;
LFG expiry, BG broadcasts and session fan-out no longer hold locks
across packets/teleports; SI/Quel'Danas/tick counters are lock-free
atomics. Watch tick-time metrics under spell-heavy load — if the spell
lock ever shows in profiles, the next step is RCU/freeze for the
template stores.

## Residual risks

`ScriptMgr` hook impls must not register scripts during dispatch;
`ConditionMgr` holds one reload of garbage; `SpellInfo` LOS-flag OR
relies on aligned-u32 atomicity; `LFG` matchmaking (tasks 1/2) keeps
its outer lock by design.
