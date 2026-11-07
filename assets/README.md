# Assets

The app fetches these through jsDelivr, each file pinned by SHA-256 in the exe. Twemoji and the font are not here: `cmake/remote_assets.txt` lists them, and they come from their own repositories at the versions in `src/assets/CMakeLists.txt`.

- `emoji/`: the hand-made emoji `01`-`07` and `1f422`, from upstream.
- Twemoji, fetched into `emoji/`: [Twemoji](https://github.com/jdecked/twemoji) © Twitter, Inc. and contributors, under [CC BY 4.0](https://creativecommons.org/licenses/by/4.0/).
- `sounds/`: made by hand by the owner of `pb29/Cloudroom`, who agreed to their use and publication here. All rights stay with the owner; no open license applies. A pack is `sounds/<device>/<pack>/`, with `pack.json` and any of `<sound>[_(hold|release)][_N].wav`; several numbered files of one sound play at random.
- Pretendard GOV Variable, fetched into `fonts/`: [Pretendard](https://github.com/orioncactus/pretendard) © Kil Hyung-jin, unmodified, under the [SIL Open Font License](https://github.com/orioncactus/pretendard/blob/v1.3.9/LICENSE), which the font also names in its own metadata.
- `words/`: comma-separated lists for random nicknames.
