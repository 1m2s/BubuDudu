# v1 demonstration recordings

These three MOV recordings were added on 4 October 2026 as downloadable
[v1 prototype pre-release assets](https://github.com/1m2s/BubuDudu/releases/tag/v1).
Hosting them with the release keeps approximately 168 MiB of video out of source
clones. The video data is unchanged.

| File | Demonstration | Size |
| --- | --- | --- |
| [ClosePeerWake.mov](https://github.com/1m2s/BubuDudu/releases/download/v1/ClosePeerWake.mov) | Nearby devices: local wake and peer wake. | 58,572,763 bytes (55.9 MiB) |
| [FarPeerWake.mov](https://github.com/1m2s/BubuDudu/releases/download/v1/FarPeerWake.mov) | Separated devices: peer wake. | 80,839,637 bytes (77.1 MiB) |
| [MovingBehavior.mov](https://github.com/1m2s/BubuDudu/releases/download/v1/MovingBehavior.mov) | Moving → settling → checking status and button interaction. | 36,482,969 bytes (34.8 MiB) |

I use these as selected demonstrations. I did not record a measured range,
counted success rate or paired serial trace. “Close” and “Far” are names, not
calibrated distances. See [final findings](../../FINAL_FIRMWARE_TEST.md) for
observations and limitations, and the [README version map](../../README.md#which-code-was-tested)
for firmware attribution.

## Integrity

The original `ClosePeerWake.MOV` has its filename extension lowercased for the
release; its contents are identical. The other filenames are unchanged.
These SHA-256 hashes identify the original video data:

| File | SHA-256 |
| --- | --- |
| ClosePeerWake.mov | `1745851debafc33f3c79b3d1e7b948e4d77216e9be16fb00cd72a2ea3eb75135` |
| FarPeerWake.mov | `039dc036baf846b8fbb419b8421175bd2afd86c06f75f210c6c1cf087307cec8` |
| MovingBehavior.mov | `f1111914727d9a3481e5d0a38b21527e354f8ca10db695ca73d34966589e437c` |
