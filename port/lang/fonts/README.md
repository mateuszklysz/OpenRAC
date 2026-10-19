# Fonts

The fonts the port draws letters with that the game's own fonts do not have (Greek, Hangul,
Chinese: game/common/glyphs.h), built into the program with the translations (`cmake/Lang.cmake`
embeds every `*.ttf` here as `fonts/<code>.ttf`, `<code>` the language's).

Each is a subset of a Noto font, under the SIL Open Font License 1.1 ([OFL.txt](OFL.txt)), made
with fontTools' `pyftsubset` from the letters of the language's translations.
