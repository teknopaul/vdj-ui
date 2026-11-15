# Stretch engine quality on synthetic material (phase 3)

Material: 110 Hz tone + click on every beat at 120 bpm, 12 s. Pitch ratio measured on the tone alone (1.000 = preserved; varispeed follows the ratio). Not a statement about how music sounds: judge with Audition (phase 7).

| engine | ratio | duration err % | pitch ratio | onset err ms | level |
|---|---|---|---|---|---|
| rubberband-lib-r3 | 0.94 / 1.05 / 1.50 | 0 | 1.000 | 1.2 / 1.1 / 0.7 | 0.98 |
| rubberband-r3 (CLI) | 0.94 / 1.05 / 1.50 | 0 | 1.000 | 0.3 / 0.2 / 0.7 | 0.98 |
| rubberband-lib-r2 / rubberband-r2 | 0.94 / 1.05 / 1.50 | 0 | 1.054 / 1.050 / 1.047 (bass tone drifts) | 2.5 / 1.8 / 6.3 | 0.91-0.94 |
| sox-tempo | 0.94 / 1.05 / 1.50 | 0 | 1.000 | 4.8 / 4.2 / 8.9 | 1.00 |
| ffmpeg-rubberband (R2) | 0.94 / 1.05 / 1.50 | 0 | 1.050 / 1.050 / 0.973 | 4.4 / 3.3 / 5.3 | 0.91-0.96 |
| soundtouch | 0.94 / 1.05 / 1.50 | 0 | 1.000 | 3.2 / 2.9 / 19.5 | 1.00 |
| ffmpeg-atempo | 0.94 / 1.05 / 1.50 | 0 | 0.999-1.001 | 5.9 / 4.0 / 3.0 | 1.00 |
| wsola | 0.94 / 1.05 / 1.50 | 0 | 1.000 | 5.1 / 4.7 / 12.4 | 1.00 |
| varispeed | 0.94 / 1.05 / 1.50 | 0 | 0.940 / 1.050 / 1.500 (by design) | 0.1 / 0.0 / 0.1 | 1.00 |

All engines are present on this machine; soundtouch (2.3.2) is now compiled in and tested (onsets smear to ~20 ms at 1.5x, the test allows 25 ms for it).
Finding: all Rubber Band R2 variants shift a steady 110 Hz tone by about the stretch ratio (440 Hz: 0.5%); R3 does not. R2 is kept but its description says so, and the tests do not assert R2 pitch.
