# v1 demonstration recordings

The author supplied three MOV recordings on 4 October 2026. They are hosted as
downloadable [v1 prototype pre-release assets](https://github.com/1m2s/BubuDudu/releases/tag/v1)
so ordinary source clones do not include approximately 168 MiB of video.
The files retain their original video data; no conversion or editing was performed.

| File | Author's description | Size |
| --- | --- | --- |
| [ClosePeerWake.mov](https://github.com/1m2s/BubuDudu/releases/download/v1/ClosePeerWake.mov) | Nearby devices: local wake and peer wake. | 58,572,763 bytes (55.9 MiB) |
| [FarPeerWake.mov](https://github.com/1m2s/BubuDudu/releases/download/v1/FarPeerWake.mov) | Separated devices: peer wake. | 80,839,637 bytes (77.1 MiB) |
| [MovingBehavior.mov](https://github.com/1m2s/BubuDudu/releases/download/v1/MovingBehavior.mov) | Moving → settling → checking status and button interaction. | 36,482,969 bytes (34.8 MiB) |

Captions follow the author's descriptions. Publication does not add a counted
success rate, measured separation, paired serial trace or full acceptance pass.
“Close” and “Far” in filenames describe the demonstrations, not calibrated
distance. Known proximity and automatic radio-transition failures remain in the
[final findings](../../FINAL_FIRMWARE_TEST.md).

The reported firmware upload target is `ecd9d4f`; later v1 closure and media
documentation commits leave the firmware unchanged. The release is explicitly
marked as a prototype pre-release with incomplete hardware acceptance.

## Provenance and integrity

Upload sources are copies in
`/Users/mohamedsellami/Desktop/repos final images/BubuDudu/videos`.
Original recordings remain in `/Users/mohamedsellami/Downloads`.
The original `ClosePeerWake.MOV` has only its filename extension lowercased in
the copy; its contents are identical. The other filenames are unchanged.

| File | SHA-256 |
| --- | --- |
| ClosePeerWake.mov | `1745851debafc33f3c79b3d1e7b948e4d77216e9be16fb00cd72a2ea3eb75135` |
| FarPeerWake.mov | `039dc036baf846b8fbb419b8421175bd2afd86c06f75f210c6c1cf087307cec8` |
| MovingBehavior.mov | `f1111914727d9a3481e5d0a38b21527e354f8ca10db695ca73d34966589e437c` |
