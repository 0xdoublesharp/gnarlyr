---------------------------- MODULE CoreHandoff ----------------------------
EXTENDS Naturals, FiniteSets

CONSTANTS Capacity, Blocks, Total, MaxOpens, LoadsReorder, CoreOneWindowsShort

ASSUME Capacity \in Nat \ {0} /\ Blocks \subseteq 1..Capacity /\ Blocks # {}
ASSUME Total \in Nat /\ MaxOpens \in Nat \ {0}
ASSUME LoadsReorder \in BOOLEAN /\ CoreOneWindowsShort \in BOOLEAN

Slots == {0, 1}
Other(s) == 1 - s
NoSlot == 2
None == 0
Fin == Capacity + 1
Largest == CHOOSE b \in Blocks : \A c \in Blocks : c <= b
Frames == 0..(Capacity - 1)
NoFrame == [gen |-> 0, index |-> 0]
Frame(g, i) == [gen |-> g, index |-> i]

VARIABLES
    ctlGen,
    state, slotGen, slotFirst, slotCount, slotResult, data,
    c1pc, c1gen, c1slot, c1filled, c1first, c1next, c1held, c1fin, c1block,
    c0pc, c0gen, c0slot, c0held, c0left, c0pos, c0expect, opens,
    conflict, stale, outOfOrder, lost

shared == <<ctlGen, state, slotGen, slotFirst, slotCount, slotResult, data>>
core1 == <<c1pc, c1gen, c1slot, c1filled, c1first, c1next, c1held, c1fin, c1block>>
core0 == <<c0pc, c0gen, c0slot, c0held, c0left, c0pos, c0expect, opens>>
ghosts == <<conflict, stale, outOfOrder, lost>>
vars == <<shared, core1, core0, ghosts>>

TypeOK ==
    /\ ctlGen \in 0..MaxOpens
    /\ state \in [Slots -> {"EMPTY", "FULL"}]
    /\ slotGen \in [Slots -> 0..MaxOpens]
    /\ slotCount \in [Slots -> 0..Capacity]
    /\ slotResult \in [Slots -> {"READY", "END"}]
    /\ c1pc \in {"idle", "checked", "decode", "recheck", "take"}
    /\ c1filled \in 0..Capacity
    /\ c1held \in {None, Fin} \cup Blocks
    /\ c1block \in {None, Fin} \cup Blocks
    /\ c0pc \in {"stopped", "play", "look", "read", "late", "done"}
    /\ c0held \in Slots \cup {NoSlot}

Init ==
    /\ ctlGen = 0
    /\ state = [s \in Slots |-> "EMPTY"]
    /\ slotGen = [s \in Slots |-> 0]
    /\ slotFirst = [s \in Slots |-> 0]
    /\ slotCount = [s \in Slots |-> 0]
    /\ slotResult = [s \in Slots |-> "READY"]
    /\ data = [s \in Slots |-> [i \in Frames |-> NoFrame]]
    /\ c1pc = "idle" /\ c1gen = 0 /\ c1slot = 0 /\ c1filled = 0 /\ c1first = 0
    /\ c1next = 0 /\ c1held = None /\ c1fin = FALSE /\ c1block = None
    /\ c0pc = "stopped" /\ c0gen = 0 /\ c0slot = 0 /\ c0held = NoSlot /\ c0left = 0
    /\ c0pos = 0 /\ c0expect = 0 /\ opens = 0
    /\ conflict = FALSE /\ stale = FALSE /\ outOfOrder = FALSE /\ lost = FALSE

WriteFrames(s, at, count, first) ==
    /\ data' = [data EXCEPT ![s] = [i \in Frames |->
                    IF i >= at /\ i < at + count THEN Frame(c1gen, first + i - at) ELSE data[s][i]]]
    /\ conflict' = (conflict \/ c0held = s)

Publish(outcome, first, count) ==
    /\ slotGen' = [slotGen EXCEPT ![c1slot] = c1gen]
    /\ slotFirst' = [slotFirst EXCEPT ![c1slot] = first]
    /\ slotCount' = [slotCount EXCEPT ![c1slot] = count]
    /\ slotResult' = [slotResult EXCEPT ![c1slot] = outcome]
    /\ state' = [state EXCEPT ![c1slot] = "FULL"]
    /\ c1slot' = Other(c1slot)
    /\ c1filled' = 0

C1Check ==
    /\ c1pc = "idle"
    /\ IF ctlGen # c1gen
         THEN /\ IF ctlGen = 0
                   THEN /\ c1gen' = 0
                        /\ UNCHANGED <<c1slot, c1filled, c1first, c1next, c1held, c1fin>>
                   ELSE /\ c1gen' = ctlGen /\ c1slot' = 0 /\ c1filled' = 0 /\ c1first' = 0
                        /\ c1next' = 0 /\ c1held' = None /\ c1fin' = FALSE
              /\ c1pc' = "idle"
         ELSE /\ ctlGen # 0 /\ ~c1fin
              /\ c1pc' = "checked"
              /\ UNCHANGED <<c1gen, c1slot, c1filled, c1first, c1next, c1held, c1fin>>
    /\ UNCHANGED <<shared, c1block, core0, ghosts>>

C1TakeHeld ==
    IF c1held = Fin
      THEN /\ Publish("END", 0, 0)
           /\ c1fin' = TRUE /\ c1held' = None
           /\ UNCHANGED <<data, conflict, c1first, c1next>>
      ELSE /\ WriteFrames(c1slot, 0, c1held, c1next)
           /\ c1first' = c1next
           /\ c1next' = c1next + c1held
           /\ c1held' = None
           /\ IF state[Other(c1slot)] = "EMPTY"
                THEN Publish("READY", c1next, c1held)
                ELSE /\ c1filled' = c1held
                     /\ UNCHANGED <<slotGen, slotFirst, slotCount, slotResult, state, c1slot>>
           /\ UNCHANGED c1fin

C1Act ==
    /\ c1pc = "checked"
    /\ IF state[c1slot] # "EMPTY"
         THEN /\ c1pc' = "idle"
              /\ UNCHANGED <<shared, c1slot, c1filled, c1first, c1next, c1held, c1fin, conflict>>
         ELSE IF c1held # None
           THEN /\ C1TakeHeld
                /\ c1pc' = "idle"
                /\ UNCHANGED ctlGen
           ELSE IF c1filled # 0 /\ Capacity - c1filled < Largest
             THEN /\ Publish("READY", c1first, c1filled)
                  /\ c1pc' = "idle"
                  /\ UNCHANGED <<ctlGen, data, c1first, c1next, c1held, c1fin, conflict>>
             ELSE /\ c1pc' = "decode"
                  /\ UNCHANGED <<shared, c1slot, c1filled, c1first, c1next, c1held, c1fin, conflict>>
    /\ UNCHANGED <<c1gen, c1block, core0, stale, outOfOrder, lost>>

C1Decode ==
    /\ c1pc = "decode"
    /\ IF c1next = Total
         THEN /\ c1block' = Fin
              /\ UNCHANGED <<data, conflict>>
         ELSE \E k \in Blocks :
                /\ k <= Total - c1next
                /\ c1block' = k
                /\ IF k <= Capacity - c1filled
                     THEN WriteFrames(c1slot, c1filled, k, c1next)
                     ELSE UNCHANGED <<data, conflict>>
    /\ c1pc' = "recheck"
    /\ UNCHANGED <<ctlGen, state, slotGen, slotFirst, slotCount, slotResult>>
    /\ UNCHANGED <<c1gen, c1slot, c1filled, c1first, c1next, c1held, c1fin>>
    /\ UNCHANGED <<core0, stale, outOfOrder, lost>>

C1Recheck ==
    /\ c1pc = "recheck"
    /\ c1pc' = IF ctlGen # c1gen THEN "idle" ELSE "take"
    /\ c1block' = IF ctlGen # c1gen THEN None ELSE c1block
    /\ UNCHANGED <<shared, c1gen, c1slot, c1filled, c1first, c1next, c1held, c1fin, core0, ghosts>>

C1Take ==
    /\ c1pc = "take"
    /\ c1pc' = "idle"
    /\ c1block' = None
    /\ IF c1block = Fin
         THEN IF c1filled # 0
                THEN /\ Publish("READY", c1first, c1filled)
                     /\ c1held' = Fin
                     /\ UNCHANGED <<c1fin, c1first, c1next>>
                ELSE /\ Publish("END", 0, 0)
                     /\ c1fin' = TRUE
                     /\ UNCHANGED <<c1held, c1first, c1next>>
         ELSE IF c1block <= Capacity - c1filled
           THEN /\ c1next' = c1next + c1block
                /\ c1first' = IF c1filled = 0 THEN c1next ELSE c1first
                /\ IF state[Other(c1slot)] = "EMPTY"
                     THEN Publish("READY", IF c1filled = 0 THEN c1next ELSE c1first, c1filled + c1block)
                     ELSE /\ c1filled' = c1filled + c1block
                          /\ UNCHANGED <<slotGen, slotFirst, slotCount, slotResult, state, c1slot>>
                /\ UNCHANGED <<c1held, c1fin>>
           ELSE /\ Publish("READY", c1first, c1filled)
                /\ c1held' = c1block
                /\ UNCHANGED <<c1fin, c1first, c1next>>
    /\ UNCHANGED <<ctlGen, data, c1gen, core0, ghosts>>

CoreOneQuiet == CoreOneWindowsShort => c1pc \notin {"checked", "take"}

C0Stop ==
    /\ c0pc \in {"play", "done"}
    /\ c0held = NoSlot
    /\ opens < MaxOpens
    /\ CoreOneQuiet
    /\ ctlGen' = 0
    /\ c0pc' = "stopped"
    /\ UNCHANGED <<state, slotGen, slotFirst, slotCount, slotResult, data>>
    /\ UNCHANGED <<core1, c0gen, c0slot, c0held, c0left, c0pos, c0expect, opens, ghosts>>

C0Open ==
    /\ c0pc = "stopped"
    /\ opens < MaxOpens
    /\ CoreOneQuiet
    /\ opens' = opens + 1
    /\ ctlGen' = opens + 1
    /\ c0gen' = opens + 1
    /\ c0slot' = 0
    /\ c0expect' = 0
    /\ c0pc' = "play"
    /\ UNCHANGED <<state, slotGen, slotFirst, slotCount, slotResult, data>>
    /\ UNCHANGED <<core1, c0held, c0left, c0pos, ghosts>>

C0Drop ==
    /\ c0pc = "play"
    /\ state' = [s \in Slots |-> IF state[s] = "FULL" /\ slotGen[s] # c0gen THEN "EMPTY" ELSE state[s]]
    /\ c0pc' = "look"
    /\ UNCHANGED <<ctlGen, slotGen, slotFirst, slotCount, slotResult, data>>
    /\ UNCHANGED <<core1, c0gen, c0slot, c0held, c0left, c0pos, c0expect, opens, ghosts>>

C0Look ==
    /\ c0pc = "look"
    /\ IF state[c0slot] # "FULL"
         THEN /\ c0pc' = "play"
              /\ UNCHANGED <<c0held, c0left, c0pos, stale, lost>>
         ELSE /\ stale' = (stale \/ slotGen[c0slot] # c0gen)
              /\ IF slotResult[c0slot] = "END"
                   THEN /\ c0pc' = "done"
                        /\ lost' = (lost \/ (slotGen[c0slot] = c0gen /\ c0expect # Total))
                        /\ UNCHANGED <<c0held, c0left, c0pos>>
                   ELSE /\ c0pc' = "read"
                        /\ c0held' = c0slot
                        /\ c0left' = slotCount[c0slot]
                        /\ c0pos' = 0
                        /\ UNCHANGED lost
    /\ UNCHANGED <<shared, core1, c0gen, c0slot, c0expect, opens, conflict, outOfOrder>>

ReadFrame ==
    /\ c0left > 0
    /\ outOfOrder' = (outOfOrder \/ data[c0held][c0pos] # Frame(c0gen, c0expect))
    /\ c0expect' = c0expect + 1
    /\ c0pos' = c0pos + 1
    /\ c0left' = c0left - 1

C0Read ==
    /\ c0pc = "read"
    /\ ReadFrame
    /\ UNCHANGED <<shared, core1, c0pc, c0gen, c0slot, c0held, opens, conflict, stale, lost>>

C0Release ==
    /\ c0pc = "read"
    /\ c0left = 0 \/ LoadsReorder
    /\ state' = [state EXCEPT ![c0held] = "EMPTY"]
    /\ c0slot' = Other(c0slot)
    /\ IF c0left = 0
         THEN /\ c0held' = NoSlot /\ c0pc' = "play"
         ELSE /\ c0pc' = "late" /\ UNCHANGED c0held
    /\ UNCHANGED <<ctlGen, slotGen, slotFirst, slotCount, slotResult, data>>
    /\ UNCHANGED <<core1, c0gen, c0left, c0pos, c0expect, opens, ghosts>>

C0LateRead ==
    /\ c0pc = "late"
    /\ ReadFrame
    /\ IF c0left = 1
         THEN /\ c0held' = NoSlot /\ c0pc' = "play"
         ELSE UNCHANGED <<c0held, c0pc>>
    /\ UNCHANGED <<shared, core1, c0gen, c0slot, opens, conflict, stale, lost>>

Finished ==
    /\ c0pc = "done"
    /\ opens = MaxOpens
    /\ UNCHANGED vars

CoreOne == C1Check \/ C1Act \/ C1Decode \/ C1Recheck \/ C1Take
CoreZero == C0Drop \/ C0Look \/ C0Read \/ C0Release \/ C0LateRead

Next == CoreOne \/ CoreZero \/ C0Stop \/ C0Open \/ Finished

Spec ==
    /\ Init
    /\ [][Next]_vars
    /\ WF_vars(C1Check) /\ WF_vars(C1Act) /\ WF_vars(C1Decode) /\ WF_vars(C1Recheck)
    /\ WF_vars(C1Take)
    /\ WF_vars(C0Drop) /\ WF_vars(C0Look) /\ WF_vars(C0Read) /\ WF_vars(C0Release)
    /\ WF_vars(C0LateRead)

NoSlotReadWhileWritten == ~conflict
NoStaleFrame == ~stale
FramesInOrder == ~outOfOrder
NoFrameLost == ~lost
HandoffSafe == NoSlotReadWhileWritten /\ NoStaleFrame /\ FramesInOrder /\ NoFrameLost

TrackPlaysOut ==
    \A g \in 1..MaxOpens :
        (c0gen = g /\ c0pc = "play") ~> (c0gen # g \/ c0pc = "done" \/ c0pc = "stopped")

DecodedSlotPlayed ==
    \A s \in Slots :
        (state[s] = "FULL" /\ slotGen[s] = c0gen /\ slotResult[s] = "READY" /\ c0pc # "stopped")
            ~> (state[s] = "EMPTY" \/ c0pc = "stopped")
NeverPlaysOut == ~(c0pc = "done" /\ opens = MaxOpens /\ ~stale /\ ~outOfOrder)
=============================================================================
