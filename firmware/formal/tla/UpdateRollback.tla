--------------------------- MODULE UpdateRollback ---------------------------
EXTENDS Naturals, FiniteSets

CONSTANTS NewImages, MaxCuts, MaxCopies, RomFallsBack, SectorAtomic, AllowBothRequest,
          WritesFail, OwnerMayDeleteUpdate

ASSUME NewImages # {} /\ "G" \notin NewImages
ASSUME MaxCuts \in Nat /\ MaxCopies \in Nat
ASSUME RomFallsBack \in BOOLEAN /\ SectorAtomic \in BOOLEAN /\ AllowBothRequest \in BOOLEAN
ASSUME WritesFail \in BOOLEAN /\ OwnerMayDeleteUpdate \in BOOLEAN

Images == {"G"} \cup NewImages
Limit == 3
NoImage == "NONE"
Content == [by : Images, fmt : {1, 2}]
NoRecord == [st |-> "NONE", att |-> 0]
Missing == [by |-> "MISSING", fmt |-> 0]
Torn == [by |-> "TORN", fmt |-> 0]
Absent == [by |-> "NONE", fmt |-> 0]
Fresh == [att |-> 0, charged |-> FALSE, due |-> FALSE, trial |-> FALSE, promoting |-> FALSE,
          settle |-> FALSE, rolledback |-> FALSE]

VARIABLES
    slot, rec, file, both,
    live, newer, bk, newest, mark,
    pc, run, m, sess, bks, cp,
    cuts, copies,
    fmt, broken,
    ownerBoth, fallbackBoth, preTrial

disk == <<slot, rec, file, both>>
store == <<live, newer, bk, newest, mark>>
cpu == <<pc, run, m, sess, bks, cp>>
env == <<cuts, copies>>
fixed == <<fmt, broken>>
ghost == <<ownerBoth, fallbackBoth, preTrial>>
vars == <<disk, store, cpu, env, fixed, ghost>>

Valid(s) == slot[s].hdr # NoImage
Good(i) == i \in Images /\ ~broken[i]

TypeOK ==
    /\ slot \in [{1, 2} -> [hdr : Images \cup {NoImage}, ovl : Images]]
    /\ rec \in [st : {"NONE", "RUNNING", "PROMOTING", "ROLLED_BACK"}, att : 0..(Limit + 1)]
    /\ file \in Images \cup {NoImage}
    /\ live \in Content \cup {Missing, Torn}
    /\ newer \in Content \cup {Absent}
    /\ bk \in [{1, 2} -> [build : Images \cup {NoImage}, data : Content \cup {Absent, Torn}]]
    /\ newest \in 0..2
    /\ mark \in Images \cup {NoImage}
    /\ cuts \in 0..MaxCuts /\ copies \in 0..MaxCopies

Init ==
    /\ slot = [s \in {1, 2} |-> [hdr |-> "G", ovl |-> "G"]]
    /\ rec = NoRecord /\ file = NoImage /\ both = FALSE
    /\ live = [by |-> "G", fmt |-> 1] /\ newer = Absent
    /\ bk = [s \in {1, 2} |-> [build |-> NoImage, data |-> Absent]]
    /\ newest = 0 /\ mark = "G"
    /\ pc = "off" /\ run = [slot |-> 1, img |-> "G"] /\ m = Fresh /\ sess = FALSE /\ bks = 1
    /\ cp = [to |-> 1, hdr |-> "G", ovl |-> "G", ok |-> "off", ko |-> "off"]
    /\ cuts = 0 /\ copies = 0
    /\ fmt \in {f \in [Images -> {1, 2}] : f["G"] = 1}
    /\ broken \in {b \in [Images -> BOOLEAN] : ~b["G"]}
    /\ ownerBoth = FALSE /\ fallbackBoth = FALSE /\ preTrial = Absent

Goto(next) == pc' = next

StartCopy(to, hdr, ovl, ok, ko) ==
    /\ cp' = [to |-> to, hdr |-> hdr, ovl |-> ovl, ok |-> ok, ko |-> ko]
    /\ Goto("cp_erase")

CopyErase ==
    /\ pc = "cp_erase"
    /\ \/ /\ slot' = [slot EXCEPT ![cp.to].hdr = NoImage]
          /\ Goto("cp_body")
       \/ /\ WritesFail
          /\ Goto(cp.ko)
          /\ UNCHANGED slot
    /\ UNCHANGED <<rec, file, both, store, run, m, sess, bks, cp, env, fixed, ghost>>

CopyBody ==
    /\ pc = "cp_body"
    /\ \/ /\ slot' = [slot EXCEPT ![cp.to].ovl = cp.ovl]
          /\ Goto("cp_head")
       \/ /\ WritesFail
          /\ Goto(cp.ko)
          /\ UNCHANGED slot
    /\ UNCHANGED <<rec, file, both, store, run, m, sess, bks, cp, env, fixed, ghost>>

CopyHead ==
    /\ pc = "cp_head"
    /\ \/ /\ slot' = [slot EXCEPT ![cp.to].hdr = cp.hdr]
          /\ Goto(cp.ok)
       \/ /\ WritesFail
          /\ Goto(cp.ko)
          /\ UNCHANGED slot
    /\ UNCHANGED <<rec, file, both, store, run, m, sess, bks, cp, env, fixed, ghost>>

PowerOn ==
    /\ pc = "off"
    /\ IF Valid(1)
         THEN /\ run' = [slot |-> 1, img |-> slot[1].hdr] /\ Goto("count")
         ELSE IF RomFallsBack /\ Valid(2)
           THEN /\ run' = [slot |-> 2, img |-> slot[2].hdr] /\ Goto("count")
           ELSE /\ Goto("dead") /\ UNCHANGED run
    /\ m' = Fresh /\ sess' = FALSE
    /\ UNCHANGED <<disk, store, bks, cp, env, fixed, ghost>>

Count ==
    /\ pc = "count"
    /\ CASE rec.st = "RUNNING" /\ rec.att < Limit ->
              /\ rec' = [rec EXCEPT !.att = rec.att + 1]
              /\ m' = [m EXCEPT !.att = rec.att + 1, !.charged = TRUE, !.due = TRUE,
                                !.trial = TRUE, !.settle = TRUE]
              /\ Goto("pass") /\ UNCHANGED cp
         [] rec.st = "ROLLED_BACK" ->
              /\ rec' = NoRecord
              /\ m' = [m EXCEPT !.rolledback = TRUE, !.settle = TRUE]
              /\ Goto("pass") /\ UNCHANGED cp
         [] rec.st = "RUNNING" /\ rec.att >= Limit /\ Valid(2) ->
              /\ StartCopy(1, slot[2].hdr, slot[2].ovl, "rb_mark", "pass")
              /\ UNCHANGED <<rec, m>>
         [] rec.st = "RUNNING" /\ rec.att >= Limit /\ ~Valid(2) ->
              /\ rec' = NoRecord /\ Goto("pass") /\ UNCHANGED <<m, cp>>
         [] rec.st = "PROMOTING" ->
              /\ m' = [m EXCEPT !.promoting = TRUE] /\ Goto("pass") /\ UNCHANGED <<rec, cp>>
         [] rec.st = "NONE" ->
              /\ Goto("pass") /\ UNCHANGED <<rec, m, cp>>
    /\ UNCHANGED <<slot, file, both, store, run, sess, bks, env, fixed, ghost>>

RollbackMark ==
    /\ pc = "rb_mark"
    /\ rec' = [st |-> "ROLLED_BACK", att |-> 0]
    /\ Goto("off")
    /\ UNCHANGED <<slot, file, both, store, run, m, sess, bks, cp, env, fixed, ghost>>

BackupSlot(build) ==
    IF \E s \in {1, 2} : bk[s].build = build THEN CHOOSE s \in {1, 2} : bk[s].build = build
    ELSE IF \E s \in {1, 2} : bk[s].build = NoImage THEN CHOOSE s \in {1, 2} : bk[s].build = NoImage
    ELSE IF newest = 0 THEN 1 ELSE 3 - newest

Pass ==
    /\ pc = "pass"
    /\ CASE ~m.settle \/ mark = run.img -> Goto("app") /\ UNCHANGED <<bks, preTrial>>
         [] m.settle /\ mark = NoImage -> Goto("mark") /\ UNCHANGED <<bks, preTrial>>
         [] OTHER -> /\ bks' = BackupSlot(mark) /\ Goto("bk_clear")
                     /\ preTrial' = IF m.trial THEN live ELSE preTrial
    /\ UNCHANGED <<disk, store, run, m, sess, cp, env, fixed, ownerBoth, fallbackBoth>>

BackupClear ==
    /\ pc = "bk_clear"
    /\ bk' = [bk EXCEPT ![bks].build = NoImage]
    /\ newest' = IF newest = bks THEN 0 ELSE newest
    /\ Goto("bk_copy")
    /\ UNCHANGED <<disk, live, newer, mark, run, m, sess, bks, cp, env, fixed, ghost>>

BackupCopy ==
    /\ pc = "bk_copy"
    /\ bk' = [bk EXCEPT ![bks].data = IF live = Missing THEN Absent ELSE live]
    /\ Goto("bk_name")
    /\ UNCHANGED <<disk, live, newer, newest, mark, run, m, sess, bks, cp, env, fixed, ghost>>

BackupName ==
    /\ pc = "bk_name"
    /\ bk' = [bk EXCEPT ![bks].build = mark]
    /\ newest' = bks
    /\ Goto("settle")
    /\ UNCHANGED <<disk, live, newer, mark, run, m, sess, bks, cp, env, fixed, ghost>>

SettleFiles ==
    /\ pc = "settle"
    /\ CASE live \in Content /\ live.fmt > fmt[run.img] -> Goto("aside_copy")
         [] newer \in Content /\ newer.fmt <= fmt[run.img] -> Goto("return_copy")
         [] OTHER -> Goto("mark")
    /\ UNCHANGED <<disk, store, run, m, sess, bks, cp, env, fixed, ghost>>

AsideCopy ==
    /\ pc = "aside_copy" /\ newer' = live /\ Goto("aside_remove")
    /\ UNCHANGED <<disk, live, bk, newest, mark, run, m, sess, bks, cp, env, fixed, ghost>>

AsideRemove ==
    /\ pc = "aside_remove" /\ live' = Missing /\ Goto("mark")
    /\ UNCHANGED <<disk, newer, bk, newest, mark, run, m, sess, bks, cp, env, fixed, ghost>>

ReturnCopy ==
    /\ pc = "return_copy" /\ live' = newer /\ Goto("return_remove")
    /\ UNCHANGED <<disk, newer, bk, newest, mark, run, m, sess, bks, cp, env, fixed, ghost>>

ReturnRemove ==
    /\ pc = "return_remove" /\ newer' = Absent /\ Goto("mark")
    /\ UNCHANGED <<disk, live, bk, newest, mark, run, m, sess, bks, cp, env, fixed, ghost>>

WriteMark ==
    /\ pc = "mark" /\ mark' = run.img /\ Goto("app")
    /\ UNCHANGED <<disk, live, newer, bk, newest, run, m, sess, bks, cp, env, fixed, ghost>>

Crash ==
    /\ pc = "app"
    /\ broken[run.img]
    /\ \/ /\ m.charged /\ rec' = [st |-> "RUNNING", att |-> Limit]
       \/ UNCHANGED rec
    /\ Goto("off")
    /\ UNCHANGED <<slot, file, both, store, run, m, sess, bks, cp, env, fixed, ghost>>

Settle ==
    /\ pc = "app"
    /\ Good(run.img) /\ m.charged
    /\ rec' = [st |-> "RUNNING", att |-> m.att - 1]
    /\ m' = [m EXCEPT !.charged = FALSE]
    /\ UNCHANGED <<slot, file, both, store, pc, run, sess, bks, cp, env, fixed, ghost>>

Shutdown ==
    /\ pc = "app"
    /\ Goto("off")
    /\ UNCHANGED <<disk, store, run, m, sess, bks, cp, env, fixed, ghost>>

Written == [by |-> run.img, fmt |-> fmt[run.img]]
Refused == live \in Content /\ live.fmt > fmt[run.img]

ChangeSettings ==
    /\ pc = "app"
    /\ Good(run.img) /\ ~Refused /\ live # Written
    /\ live' = Written
    /\ UNCHANGED <<disk, newer, bk, newest, mark, cpu, env, fixed, ghost>>

TornSettings ==
    /\ ~SectorAtomic /\ cuts < MaxCuts
    /\ pc = "app"
    /\ Good(run.img) /\ ~Refused
    /\ live' = Torn /\ cuts' = cuts + 1 /\ Goto("off")
    /\ UNCHANGED <<disk, newer, bk, newest, mark, run, m, sess, bks, cp, copies, fixed, ghost>>

FileChoices ==
    {file} \cup (IF copies < MaxCopies THEN NewImages ELSE {})
           \cup (IF OwnerMayDeleteUpdate THEN {NoImage} ELSE {})

BothChoices == IF AllowBothRequest THEN BOOLEAN ELSE {both}

SessionWith(f, b, c) ==
    /\ pc = "app"
    /\ Good(run.img)
    /\ rec' = IF m.due THEN [st |-> "RUNNING", att |-> IF m.charged THEN m.att + 1 ELSE m.att] ELSE rec
    /\ m' = [m EXCEPT !.due = FALSE, !.charged = FALSE]
    /\ file' = f /\ both' = b /\ sess' = c
    /\ copies' = IF f # file /\ f \in Images THEN copies + 1 ELSE copies
    /\ Goto("updater")
    /\ UNCHANGED <<slot, store, run, bks, cp, cuts, fixed, ghost>>

Session == \E f \in FileChoices, b \in BothChoices, c \in BOOLEAN : SessionWith(f, b, c)
ConfirmSession == SessionWith(file, both, TRUE)

Updater ==
    /\ pc = "updater"
    /\ CASE file = NoImage ->
              Goto("sessend") /\ UNCHANGED <<file, cp, ownerBoth, fallbackBoth>>
         [] Valid(1) /\ slot[1].hdr = file ->
              file' = NoImage /\ Goto("sessend") /\ UNCHANGED <<cp, ownerBoth, fallbackBoth>>
         [] both \/ ~Valid(2) ->
              /\ StartCopy(2, file, file, "u_slot1", "sessend")
              /\ ownerBoth' = (ownerBoth \/ both)
              /\ fallbackBoth' = (fallbackBoth \/ ~both)
              /\ UNCHANGED file
         [] OTHER ->
              Goto("t_rec") /\ UNCHANGED <<file, cp, ownerBoth, fallbackBoth>>
    /\ UNCHANGED <<slot, rec, both, store, run, m, sess, bks, env, fixed, preTrial>>

UpdaterSlot1 ==
    /\ pc = "u_slot1"
    /\ StartCopy(1, slot[2].hdr, slot[2].ovl, "u_del", "sessend")
    /\ UNCHANGED <<disk, store, run, m, sess, bks, env, fixed, ghost>>

UpdaterDelete ==
    /\ pc = "u_del" /\ rec' = NoRecord /\ Goto("u_both")
    /\ UNCHANGED <<slot, file, both, store, run, m, sess, bks, cp, env, fixed, ghost>>

UpdaterBoth ==
    /\ pc = "u_both" /\ both' = FALSE /\ Goto("u_file")
    /\ UNCHANGED <<slot, rec, file, store, run, m, sess, bks, cp, env, fixed, ghost>>

UpdaterFile ==
    /\ pc = "u_file" /\ file' = NoImage /\ Goto("off")
    /\ UNCHANGED <<slot, rec, both, store, run, m, sess, bks, cp, env, fixed, ghost>>

TrialRecord ==
    /\ pc = "t_rec"
    /\ rec' = [st |-> "RUNNING", att |-> 0]
    /\ StartCopy(1, file, file, "u_file", "t_failed")
    /\ UNCHANGED <<slot, file, both, store, run, m, sess, bks, env, fixed, ghost>>

TrialFailed ==
    /\ pc = "t_failed"
    /\ rec' = IF Valid(1) THEN NoRecord ELSE rec
    /\ Goto("sessend")
    /\ UNCHANGED <<slot, file, both, store, run, m, sess, bks, cp, env, fixed, ghost>>

RunningFromSlot1 == slot[1].ovl = run.img

PromotionDue ==
    /\ (m.trial /\ sess) \/ m.promoting
    /\ (rec.st = "RUNNING" /\ sess) \/ rec.st = "PROMOTING"
    /\ RunningFromSlot1

SessionEnd ==
    /\ pc = "sessend"
    /\ Goto(IF PromotionDue THEN "p_rec" ELSE "app")
    /\ sess' = FALSE
    /\ UNCHANGED <<disk, store, run, m, bks, cp, env, fixed, ghost>>

PromoteRecord ==
    /\ pc = "p_rec"
    /\ rec' = [st |-> "PROMOTING", att |-> 0]
    /\ StartCopy(2, slot[1].hdr, slot[1].ovl, "p_del", "app")
    /\ UNCHANGED <<slot, file, both, store, run, m, sess, bks, env, fixed, ghost>>

PromoteDelete ==
    /\ pc = "p_del"
    /\ rec' = NoRecord
    /\ m' = [m EXCEPT !.trial = FALSE, !.promoting = FALSE]
    /\ Goto("app")
    /\ UNCHANGED <<slot, file, both, store, run, sess, bks, cp, env, fixed, ghost>>

RecordSteps == {"count", "rb_mark", "t_rec", "p_rec", "p_del", "u_del"}

Cut ==
    /\ pc \notin {"off", "dead"}
    /\ cuts < MaxCuts
    /\ cuts' = cuts + 1
    /\ Goto("off")
    /\ IF ~SectorAtomic /\ pc \in RecordSteps THEN rec' \in {rec, NoRecord} ELSE UNCHANGED rec
    /\ UNCHANGED <<slot, file, both, store, run, m, sess, bks, cp, copies, fixed, ghost>>

Dead ==
    /\ pc = "dead"
    /\ UNCHANGED vars

Steps ==
    \/ Count \/ RollbackMark \/ CopyErase \/ CopyBody \/ CopyHead
    \/ Pass \/ BackupClear \/ BackupCopy \/ BackupName \/ SettleFiles
    \/ AsideCopy \/ AsideRemove \/ ReturnCopy \/ ReturnRemove \/ WriteMark
    \/ Updater \/ UpdaterSlot1 \/ UpdaterDelete \/ UpdaterBoth \/ UpdaterFile
    \/ TrialRecord \/ TrialFailed \/ SessionEnd \/ PromoteRecord \/ PromoteDelete

Next ==
    \/ PowerOn \/ Steps \/ Crash \/ Settle \/ ChangeSettings \/ TornSettings \/ Shutdown
    \/ Session \/ Cut \/ Dead

Spec ==
    /\ Init /\ [][Next]_vars
    /\ WF_vars(PowerOn) /\ WF_vars(Steps) /\ WF_vars(Crash) /\ WF_vars(Settle)

SpecOwnerConfirms == Spec /\ SF_vars(ConfirmSession)

AlwaysBootable == Valid(1) \/ (RomFallsBack /\ Valid(2))

NoOverwriteBeforeVerify ==
    (pc \in {"cp_body", "cp_head"}) => Valid(3 - cp.to)

PreviousImageKept ==
    \E s \in {1, 2} : Valid(s) /\ Good(slot[s].hdr) /\ (s = 1 \/ RomFallsBack)

KnownGoodKept == ownerBoth \/ PreviousImageKept

SettingsAvailable ==
    \/ live \in Content
    \/ newer \in Content
    \/ \E s \in {1, 2} : bk[s].build # NoImage /\ bk[s].data \in Content

AfterRollback == pc = "app" /\ m.rolledback

RollbackRestoresImage == AfterRollback => (Valid(1) /\ slot[1].hdr = slot[2].hdr)

RollbackRestoresSettings == (AfterRollback /\ preTrial \in Content) => live = preTrial

PreviousSettingsRecoverable ==
    (AfterRollback /\ preTrial \in Content) =>
        \/ live = preTrial
        \/ \E s \in {1, 2} : bk[s].build # NoImage /\ bk[s].data = preTrial

TrialResolves == (rec.st = "RUNNING") ~> (rec.st # "RUNNING")

RestoredBuildCanSave == (pc = "app" /\ Good(run.img) /\ rec.st = "NONE" /\ ~m.trial) => ~Refused

RollbackWitness == ~AfterRollback
PromotionWitness == ~(pc = "p_del")
=============================================================================
