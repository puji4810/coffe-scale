# Current flow policy (2026-10-18)

The firmware now uses `scale/flow_rate.hpp`: a trailing **1.2 s** linear
weight regression and a fixed **100 ms** publication EMA. There is no
Kalman filter, impact gate/rewind/hold, boost, rate ceiling, stop latch,
restart proof or adopted resume rate. Every valid sample contributes to
flow; the existing ±30 g/s clip and 0.3 g/s deadband affect display only.
Gross single-sample level jumps (>20 g AND >60 g/s) translate the weight
origin while retaining the slope; they never suppress/re-arm pouring.
Gaps over 250 ms discard stale evidence and re-prime from the next sample.
Duplicate/backward timestamps and non-finite weights are ignored.

`brew-20261015-weight.csv` and `brew-20261017-weight.csv` come from the
user's exported brews. Rounded duplicate timer timestamps keep their
last weight; the app replay interpolates to 80 SPS. These are filtered
weight proxies, not raw ADC/IMU reconstructions. On identical input the
previous working-tree estimator produced 263 zero-flow samples in the
15 October 85–115 s sustained-growth interval; the new output has none,
with minimum 1.519 g/s and mean 2.907 g/s. The 17 October 45–65 s interval
also has no zeros (minimum 2.066 g/s). Their mean rates must agree with
recorded net weight growth within 0.2 g/s.

All original raw fixture files below remain mandatory. The continuous
3 October disturbance replay now has minimum 3.271 g/s and mean
3.710 g/s; the 4 October restart minimum is 3.535 g/s. The new regressions
require >=2 g/s during those confirmed growth windows. The quiet
3 October interval has peak 0.375 g/s and near-zero signed mean; small
real weight motion may remain visible instead of being locked to zero.

The tradeoff is explicit: clean 70% rate changes sustain their threshold
in about 0.89 s, with a 1.2 s test budget; a clean stop reaches the display
deadband within 1.5 s. These are injected-time/replay measurements, not
synchronized hand-action latency guarantees. The previous state machine's
sub-0.4 s onset/stop and exact-zero-under-ringing assertions are retired.
All 36 frozen pulse intervals still require sustained positive responses:
30% of their fixed weight-derived rate within 1.2 s, including sub-second
pulses whose smoothed response extends past the stop but precedes the
next pour. The splash replay additionally checks its settled rate and
held stop. Placement/removal can still create short transients, especially
when spread over several samples; there is no claim that weight alone
can distinguish every disturbance from water.

The regression's current-weight level is bounded between the measured
window mean and latest sample for **weight-display lag compensation
only**; this prevents placement overshoot without changing the flow.
The complete weight/zero/tare/display tests remain enabled. Publication
smoothing must not change their results. `fboost`/`fresume` CSV columns
remain as zero-filled legacy fields; they no longer describe runtime
states. `ftrip=1`/`disturbed` mark a single gross level discontinuity.

## Previous gated-estimator investigation (historical; policy superseded)

`brew-continuous-weight.csv` contains the full weight curve from the
2026-10-01 exported brew (103.6 s). The user confirmed continuous pouring
after a pause around 50 s; the regression checks 56–75 s. Equal exported
timer timestamps retain the last weight, and the test interpolates the
remaining points to 80 Hz before feeding the production `scale::app`.
This is a diagnostic proxy from approximately 10 Hz filtered weight,
not a raw ADC capture: unseen weight and IMU disturbances cannot be
reconstructed. The original proxy snapped into the zero display band
for 17 samples (212.5 ms) despite sustained growth. The minimized
65–68 s slice also reproduced the failure without a disturbance gate.
The regression forbids zero throughout the confirmed pour interval.
A separate constant 3 g/s pour with a 0.2 g, 3 Hz measurement ripple
checks continuity, mean accuracy, and variance; it also checks a real
stop afterward.

`brew-20261003-weight.csv` contains 995 distinct timestamps from the
107.9 s exported brew supplied on 2026-10-03. Repeated rounded timestamps
retain the last weight, and the test interpolates to 80 Hz through
`scale::app`. The confirmed growth intervals, 32–49.4 s and 60–86 s,
exclude placement/removal, the pause around 50 s, and start/stop
transients. Published flow must have no zero samples, a peak below
8 g/s, and standard deviation below 1.2 g/s. This remains a filtered
weight proxy, with the same raw ADC/IMU limitations described above.

Published flow now uses a time-based adaptive smoother after the gated
KF. A steady pour rejects short rate excursions; supported changes use
a faster response, and proven stillness bypasses smoothing. Weight lag
compensation and weight holding retain the original estimator state.
`replay::driver` exports the published `flow_gps` and appends the
unsmoothed `flow_state_gps`, so both paths can be tested independently.
The frozen capture bounds described below apply to the unsmoothed
estimator and remain unchanged. Published flow separately checks all
32 onset intervals in the latest, rapid and noisy captures: each crossing
must hold for 150 ms, with a 0.425 s bound for previously faster onsets
and the existing longer bounds for noisy onsets. The published splash
stop must hold zero within 0.50 s and stay in the deadband afterward;
all three full captures must retain exact zero in their final quiet
intervals. A first estimator zero can precede proven stillness and is
not used as a published-stop reference.

A synthetic 6 g/s pour with 0.4 g, 3 Hz measurement ripple checks mean
accuracy, standard deviation below 0.2 g/s and peak below 6.5 g/s. A
separate clean 0→6→8→5→0→6→0 sequence checks sustained rate crossings
within 0.45 s and exact zero within 0.40 s of stopping. An identical-input
comparison with smoothing disabled requires bit-identical continuous
weight, displayed weight and estimator flow. These tests establish
replay behavior, not measured device latency from hand-action markers.

`pour-onset.txt` and `pulsed-pour.txt` are local recorded raw captures,
kept in the test source tree because `firmware/captures/` is ignored.
They preserve the captured header and W/A/event order; no physical-action
markers or exact device commit hashes were recorded.

The tests replay through `replay::driver` and `scale::app`. The pulsed-pour
reference seconds and positive thresholds are fixed from the offline
weight-curve review; they are not synchronized to hand motion. Onset checks
require a full 150 ms positive crossing within the same pour interval.
Stop checks include a full zero hold and the later residual peak.

The first pulsed-pour stop permits a small post-zero residual (up to
0.40 g/s); the final stop permits 0.40 s held-zero latency. These are
explicit regression tolerances, not proof that the 0.30 s clean-stop goal
or perfect zero under slosh has been achieved.

`splash-restart.txt` is the 32.5–37.709 s slice of
`raw-20261001-115032.txt`, preserving its header and stream order. Its
412 weight samples reproduce the full capture's delayed post-splash
restart: the conservative pre fits resumed at 3.46 g/s despite a clean
8.96 g/s fast ramp, taking 0.703 s to sustain the fixed 5.027 g/s
threshold. The regression requires a full 150 ms crossing within
0.40 s of the frozen 34.155 s weight reference, plus sustained flow and
bounded mean/peak during the pour. A separate check bounds every sample
from the first displayed zero at 36.653 s through the end of the quiet
interval (0.25 s before the next weight onset) to the 0.30 g/s deadband.
This interval previously resurfaced at 0.995 g/s even after an apparent
zero; its zero hold had never engaged. The current replay sustains flow
in 0.389 s and keeps the target quiet interval at displayed zero. These
references are not physical action timestamps, and the onset bound does
not claim the 0.30 s target.

`latest-pulses.txt` preserves the complete `raw-20261001-115032.txt`
capture (3327 W samples). All eight fixed reference intervals must pass,
so improving the late splash onset cannot hide an earlier onset
regression. The first three stops still permit recorded residual peaks
of 0.55, 0.95, and 1.30 g/s respectively; the current peaks are 0.505,
0.908, and 1.258 g/s. Those tolerances preserve the existing behavior
and do not claim that every stop is now perfectly zero. The final stop
also retains a 0.69 s held-zero bound because its recorded settling
motion remains longer than the clean-stop target.

`rapid-pulses.txt` and `noisy-pulses.txt` preserve the full captures
`raw-20261001-124228.txt` (3138 W) and `raw-20261001-124802.txt`
(2269 W), including the header and W/A/C order. The comparison starts
from the preceding working-tree estimator, not Git HEAD. Three target
restarts formerly required 0.502439, 0.803776 and 0.439576 s to sustain
their fixed 70% thresholds for 150 ms; they now require 0.389400,
0.351652 and 0.389340 s. All three have a 0.40 s regression bound and
must also maintain the threshold through the middle of the pour.

The full-capture guards freeze all 24 weight-reference intervals. The
other 21 onsets retain their former bounds with about two samples of
margin. These references were inferred from the median-domain weight
trend, not recorded hand-action markers. Some intervals in the rapid
capture are closely spaced or contain interrupted pouring; a zero_max
of 0 explicitly means no baseline 150 ms zero run was found before the
next onset's 0.25 s exclusion. Those intervals check onset only, and
must not be counted as successful stable-zero stops.

The noisy capture's third and fourth stops retain 0.34 and 0.59 g/s
post-zero tolerances (measured peaks 0.309 and 0.557 g/s). Its second
stop now holds zero after 0.238672 s, about one sample later than before;
the other measured zero holds are unchanged. Later noisy onsets still
include a 0.552658 s delay; the rapid capture retains delays up to
0.602914 s. Passing the fixtures does not establish a universal 0.30 s
response or exact zero under settling motion.

`pour-disturbance-20261003.txt` and `idle-disturbance-20261003.txt`
come from `raw-20261003-193758.txt` (10612 W, 13596 A, 4 E, 66 C).
The user described blooming followed by two approximately steady pours.
Brew-relative time is capture time minus the timer-toggle timestamp,
19.821407 s. The fixtures retain stream order and calibration, covering
brew seconds 62–66 (312 W) and 100–104 (307 W). Their header tare is
translated to the first retained weight so replay priming does not
introduce a placement step. They are minimized reproductions, not
substitutes for replaying the complete capture with its original events.

The pour guard checks 63.70–65.05 s: the previous published readout had
19 zero samples in the minimized replay and 41 in the full capture.
It now requires no zeros and a minimum of 1.50 g/s. The idle guard checks
101.50–103.50 s; the previous minimized replay manufactured a peak of
6.189 g/s in magnitude, and every published sample must now remain
within the 0.30 g/s deadband. Times are weight-derived references, not
synchronized hand-action markers.

The readout maintains continuity through disturbance gates without
restarting warm-up for an established pour. After proven rest, sustained
aligned weight growth must earn a restart. A broad positive weight trend
can protect against a brief KF trough, but cannot raise the previous
published rate by itself. Clean changes and proven stops retain their
fast path. Synthetic 0.35, 0.60 and 1.00 g/s ramps guard against suppressing
real slow pours; the existing pulse-onset and zero-hold bounds remain
unchanged. Weight output and the underlying KF state are checked
independently of the published readout.

`restart-disturbance-20261004.txt` retains brew seconds 60–70 from
`raw-20261004-155450.txt` (12210 W, 15342 A, 5 E, 76 C). Timer start is
17.006696 capture seconds. The slice preserves W/A/C order and includes
796 W samples; its header tare is translated to its first weight, as
for the October 3 slices. The user confirmed bloom plus two pours and
noticeably improved screen flow with the preceding firmware.

This slice reproduces two remaining defects with the production app:
a 7.311 g/s spike while weight remains near its plateau at 63.2–64.2 s,
and 42 zero readings despite renewed weight growth at 68.1–68.65 s.
The guards bound the plateau spike to 1.50 g/s and require no zeros and
at least 1.50 g/s during the target restart interval. These bounds
address the captured defects; they are not measured hand-action latency.

A broad flat trend with low KF velocity can establish rest even when
individual short windows are noisy. Restart growth must exceed the
long-window noise and, while the broad trend remains slow, the broad
window noise too. This blocks a short boost from a weight bounce while
preserving the faster path for established growth. During a newly
started pour, supported growth protects the existing published flow
before the usual warm-up completes. Growth exceeding twice the broad
fit residual may provide that support under stronger noise. This only
preserves the previous flow, never raises it without further evidence.
All older pulse latency and zero-hold tolerances are unchanged.
