# Brew flow replays

`brew-20261015.json` and `brew-20261017.json` are the user's exported brews
used to investigate unstable saved flow curves. Keep their original `t`, `w`
and device `f` arrays intact. Both contain repeated 100 ms timer timestamps;
the 15 October recording also includes a large removal/replacement of load.

Run `node web/brew_flow_test.mjs` from the firmware directory. The regression
requires both files. It checks weight-derived flow during sustained growth,
integrated flow against recorded weight gain, and rejection of load jumps.
The recorded device flow is retained for comparison, not used as ground truth.

Saved brews retain raw `f[]` and add `flowFit: {version, windowS, f[]}` at timer
reset. Version 1 uses a centered 3-second robust local linear weight fit.
`breaks[]` records resume/gap boundaries that compressed timer time cannot
represent. Gross load jumps and time gaps also split the fit. Unknown regions
are `null`; slopes retain their sign so noise is not rectified into extra water.
The fit smooths events shorter than its window and cannot distinguish every
physical disturbance from water. Old brews are fitted on demand, without a
database migration or changes to their original readings.
