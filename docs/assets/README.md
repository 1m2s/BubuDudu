# Real validation assets

No hardware photos, instrument captures or new bench traces are included here yet. DEVLOG excerpts are historical reports, not substitute raw evidence.

When real evidence is available, use these directories (create them when adding files):

| Directory | Contents |
| --- | --- |
| `hardware/` | Actual board/wiring photographs with readable pin labels and revision |
| `logic-analyzer/` | Original capture plus exported image/data, sampling rate and channel mapping |
| `serial/` | Paired Bubu/Dudu text logs and the exact test procedure |

Keep architecture diagrams in Mermaid Markdown with their documentation. They explain implementation and must not resemble fabricated measurement traces.

Use descriptive names such as `YYYY-MM-DD_<commit>_<scenario>_bubu.txt`. Alongside each capture record firmware commit, build environment, board identity, wiring changes, power source, test stimulus, expected result, observed result and known limitations. Identify which board initiated the operation. Preserve original timestamps and units.

Do not manufacture missing screenshots, waveforms or photographs. Review real captures for personal paths, device identifiers and credentials before publication; document intentional redaction without changing the measured result. Avoid committing generated binaries or large redundant exports. The repository does not ignore this directory.
