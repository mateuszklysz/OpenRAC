# Fonts

The fonts the port draws letters with that the game's own fonts do not have, built into the
program with the translations (`cmake/Lang.cmake` embeds every `*.ttf` here as `fonts/<name>.ttf`):

| File | Used for | From |
|---|---|---|
| `latin.ttf` | the captions before the story movies, in a language without a font of its own | Noto Sans Bold |

Each is a subset of a Noto font, under the SIL Open Font License 1.1 ([OFL.txt](OFL.txt)).
`latin.ttf` was made with fontTools:

```sh
pyftsubset NotoSans-Bold.ttf --unicodes="U+0020-007E,U+00A0-024F,U+2013-2014,U+2018-201E,U+2026" \
    --layout-features='' --no-hinting --desubroutinize --output-file=latin.ttf
```
