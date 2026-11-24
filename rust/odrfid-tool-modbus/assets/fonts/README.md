# Fonts

Embedded into the binary:

| File | Source | License |
| --- | --- | --- |
| `Inter-Regular-subset.ttf`, `Inter-SemiBold-subset.ttf` | [Inter 4.1](https://github.com/rsms/inter/releases/tag/v4.1), `extras/ttf`; the same files as in `rust/wdtmon-rs/assets/fonts/` | SIL OFL 1.1, `LICENSE-Inter.txt` |
| `Phosphor-subset.ttf` | [Phosphor icons](https://phosphoricons.com) regular, as shipped in `egui-phosphor` 0.14 (`res/Phosphor.ttf`) | MIT, `LICENSE-Phosphor.txt` |

Inter keeps Latin, Latin-1/Extended-A, Cyrillic and common punctuation.

Phosphor keeps the codepoints listed in `src/glyph.rs`:

```sh
pyftsubset Phosphor.ttf --no-hinting \
  --unicodes="$(grep -o 'u{[0-9A-F]*}' ../../src/glyph.rs | tr -d 'u{}' | sed 's/^/U+/' | paste -sd,)" \
  --output-file=Phosphor-subset.ttf
```
