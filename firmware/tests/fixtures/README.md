# Flow Replay Fixtures

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
stop afterward. Existing raw-capture onset/stop bounds remain in force.

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
