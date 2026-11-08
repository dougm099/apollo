# Decision Specs — what should "match" mean?

**This is a decision aid, not a decision.** Three concrete definitions of
"match," each written **in producer language first, DSP second**, with honest
pros/cons and implementation difficulty. Pick one to build first — the
skeleton's analysis module is structured so the others can be added later.

---

## Spec 1 — Groove-lock: tempo + key alignment

> **One-liner (producer):** *Drop any sample in and it automatically plays in
> time and in key with your track.*

**In the studio:** You pull a 100 BPM loop in F minor into a 128 BPM track in
A minor. Instead of warping it by hand, guessing the original tempo, and
pitching it by ear, Apollo listens to your master, figures out the tempo and
key, and the sample just… fits. Like Ableton's warp meets auto-key-detect,
but it never stops listening — if the track changes, the sample follows.

**Under the hood (DSP):** real-time tempo estimation on the master
(onset-envelope → autocorrelation, `dsp-survey.md` §1) + chroma key detection
(§2); the sample is time-stretched (Rubber Band / Elastique, §5) and
pitch-shifted to the detected targets; stretcher latency is reported to the
DAW for delay compensation.

**Pros**
- Instantly audible. Anyone — producer or not — hears it working in seconds.
- Clear, marketable value: it does a tedious job producers actually do by hand.
- Pairs naturally with Ableton's warp workflow; feels native to Live users.

**Cons**
- Time-stretching has audible artifacts at extreme ratios; key detection is
  the least reliable analysis (~70–80% on tonal material) — a wrong key
  auto-applied sounds *broken*, not helpful.
- Adds real latency (stretcher lookahead); must be delay-compensated or the
  sample feels late.
- Needs a licensed stretcher library for professional quality (cost/effort).

**Difficulty: High.** External stretcher integration + latency compensation +
threading + confidence gating on key. Rough scope: the full analysis engine
plus a licensed DSP dependency. This is the flagship, not the starter.

**Demoability: ★★★★★ — the most demoable option.** See verdict below.

---

## Spec 2 — Sonic glue: spectral (EQ) matching

> **One-liner (producer):** *Makes the sample sound like it was recorded for
> your track — it matches the sample's tone to your master so it sits in
> the mix.*

**In the studio:** Your master is dark and warm; the sample you found is
bright and harsh. Instead of reaching for an EQ and sweeping bands by ear,
Apollo continuously nudges the sample's tonal balance toward the master's —
dulling the harshness, filling what's missing. It is the "make it sit"
knob. Subtle when right, which is the point: finished records sound like
everything belongs together.

**Under the hood (DSP):** match coarse spectral envelope — per-band energies
across 8–32 log-spaced bands (§3), smoothed with 50–200 ms envelope
followers → apply corrections with a bank of peaking/shelf IIR filters,
clamped to ±6 dB so a strange master can't wreck the sample.

**Pros**
- No timing artifacts, no latency to compensate — the safest DSP of the three.
- Works on *any* material, including atonal/textural sounds where key
  detection fails.
- Genuinely "pro" behavior: tonal cohesion is half of what mixing engineers
  do all day.

**Cons**
- Subtle by design — there is a real risk a listener thinks "I can't hear
  what it does," which is a marketing problem even when the engineering is
  right.
- Can dull transients or homogenize character if over-applied; needs careful
  clamping and the Match Amount control.
- Harder to demo than Spec 1 (see below).

**Difficulty: Medium.** Filterbank + envelope followers, all inside JUCE's
`dsp` module — no external dependencies, no license costs. The work is in
tuning (smoothing times, clamp ranges), not in new algorithms.

**Demoability: ★★★☆☆.** Best shown A/B on a harsh sample against a warm
master — audible, but you have to set the scene for the listener.

---

## Spec 3 — Level-ride: loudness conforming

> **One-liner (producer):** *Keeps the sample at exactly the right volume
> against your master — never too loud, never buried.*

**In the studio:** Every producer knows the loop that sounds perfect at one
volume and wrong at another. Apollo measures the perceived loudness (LUFS)
of your master and rides the sample's gain to match — like a mastering
engineer's hand on the fader, following the track's dynamics. Unsexy.
Essential. The kind of thing you miss the moment you turn it off.

**Under the hood (DSP):** ITU-R BS.1770 loudness on both signals via
libebur128 (MIT) or hand-rolled K-weighting (§4); match the sample to the
master's short-term (3 s) LUFS with 100–300 ms gain smoothing to avoid
pumping and zipper noise.

**Pros**
- The most *reliable* analysis of the three — loudness is a measurement
  standard, not an estimate. It will not "guess wrong."
- Smallest implementation, fastest to a working plugin.
- Level-matching is legitimately half of "sitting in the mix" — producers
  will feel this one even if they can't name it.

**Cons**
- Least exciting story: "it's intelligent auto-gain" is a harder sell than
  "it puts your sample in key."
- Alone, it doesn't address tone or timing — it is one dimension of "match."

**Difficulty: Low–Medium.** One MIT-licensed library (or ~150 lines of
hand-rolled filters) + gain smoothing. A competent first milestone: a
working, shippable-feeling plugin fastest.

**Demoability: ★★★★☆.** Play a quiet sample against a loud master, bypass
and un-bypass — the difference is obvious in two seconds. Less magical than
Spec 1, more convincing than Spec 2.

---

## Verdict: which is most demoable to a non-technical producer?

**Spec 1 (Groove-lock), by a clear margin.** The demo is: drop a wrong-tempo,
wrong-key sample into a playing track, and it snaps into place *while the
music plays*. No explanation needed, no golden ears required — a
non-technical producer hears "before it clashed, now it fits" instantly. It
is the only option whose effect is both dramatic and self-evident.

Spec 3 demos well but feels like a utility; Spec 2 is the connoisseur's
choice and needs a guided A/B to land.

**Suggested path (not a decision):** build Spec 3 first (fastest working
plugin, proves the routing/analysis architecture), then Spec 1 as the
flagship demo — with Spec 2's spectral glue folded in later as the "polish"
layer. All three share the same analysis threading; they compose.
