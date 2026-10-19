# Translations

Two kinds of text can be translated, each as one
[gettext](https://www.gnu.org/software/gettext/manual/html_node/PO-Files.html)
file a language, edited with any text editor or a PO editor such as
[Poedit](https://poedit.net/). Nobody edits code.

- **The game's texts**: menus, the HUD, help boxes, banners and subtitles,
  every one of them. They come from your disc, so their files are made on your
  computer and stay there (below, [The game's texts](#the-games-texts)).
- **The port's own words**: the rows and values of the Video options page it
  adds to the game's menus, and the message boxes a player may see when the
  game cannot start. These files are in this folder and built into the
  program.

Both follow the Language row of the Video options page (the settings'
`language`): `System` picks the system's language when there is a translation
for it.

## The game's texts

At its first start the port reads every text of your disc and writes it to
`settings/lang/game/` in the port's data folder (`~/.local/share/openrac` on
Linux, `~/Library/Application Support/OpenRAC` on macOS; the log's first lines
say where):

| File | |
|---|---|
| `game.pot` | the template: every English text, with its id, empty |
| `en.po`, `fr.po`, `de.po`, `es.po`, `it.po` | each language your disc has, beside the English (a US disc has English only) |

These files hold the disc's texts: never commit, upload or share them with
OpenRAC (docs/policy/SOURCING.md). To translate the game into another
language, copy `game.pot` to `<code>.po` in the same folder (`pl.po`), add the
`Language:` and `X-Language-Name:` lines to its header as
[Translating](#translating) shows, and fill in each `msgstr`. Choose the language on the
Language row and the game shows your words; a text left empty stays English.
Editing `fr.po` changes the French the same way. Delete `game.pot` and the
port writes the files again at its next start (your own `<code>.po` stays).

In a game text:

- `msgctxt` is the text's id and `#:` where it is used (`all_text`: menus and
  the HUD; `level 05`: that level's help and subtitles). Leave both alone.
- A word in brackets is a button or a colour: `[cross]`, `[circle]`,
  `[triangle]`, `[square]`, `[L1]`, `[R1]`, `[L2]`, `[R2]`, `[lstick]`,
  `[rstick]`; `[orange]` (names), `[blue]`, `[green]`, `[purple]`, and
  `[default]` back to the text's colour. `[xNN]` is another byte the game
  uses. Keep them, moved to where the sentence needs them. A `[` of the text
  itself is written `[[`.
- A new line is `\n`. Help boxes wrap their lines themselves.
- The game's fonts have the vowels with an accent (á à â ä, é è ê ë and so
  on); any other letter is drawn plain: ą as a, ł as l, ż as z. Write them
  normally anyway: a later port may draw them.
- The game itself is left in English underneath: its own Language page set
  to another language shows that language from the disc instead.

## The port's own words

Each language is one file, `<code>.po`, in this folder. Every `.po` file
here is built into the program.

| File | |
|---|---|
| [openrac.pot](openrac.pot) | the template: every English text, empty. Generated from the code; do not edit it |
| [pl.po](pl.po) | Polski |

### Translating

1. Copy `openrac.pot` to `<code>.po`, where the code is the language's
   ([ISO 639-1](https://en.wikipedia.org/wiki/List_of_ISO_639_language_codes),
   with a region when it matters: `de`, `pt_BR`).
2. In its header, after the `Content-Type` line, add the language's code and
   its name in its own words, the way the Language row shows it:

   ```po
   msgid ""
   msgstr ""
   "Content-Type: text/plain; charset=UTF-8\n"
   "Language: de\n"
   "X-Language-Name: Deutsch\n"
   ```

3. Fill in each `msgstr`. One left empty shows the English.

Keep:

- **`{}` as they are**, as many and in the same order: each is a number or a
  name filled in when the text is shown (`{} (not connected)`).
- **The Video options page's texts short**, about the English length. They
  are drawn in the game's font, in a row of fixed width, and the font has
  only the letters of the disc's languages: accented letters are drawn plain
  (`ą` as `a`, `ß` as `ss`), so write them normally. A text longer than 47
  letters is cut.
- **The file in UTF-8.**

### Trying a translation without building

Put the `.po` file in a `lang` folder beside the port's settings file:
`settings/lang/` in the port's data folder (`~/.local/share/openrac` on Linux,
`~/Library/Application Support/OpenRAC` on macOS), or beside the file
`--settings` names; the launcher passes its own, and the log's first lines
say which. The port reads every `.po` there at start, in place
of a built-in one of the same language. Then choose it on the Language row of
the Video options page, or set `language = <code>` in the settings file.
`auto` (System) picks the system's language when there is a translation for
it. The log says which language the port uses and which files it read.

### Checking

```sh
cd port
python3 tools/i18n_template.py --check   # the template is the code's; each .po's texts and {} match it
ctest --preset release -R i18n           # the files read, and the Video page's texts fit
```

The first says how many texts each language still lacks.

### For contributors: a new text in the code

Write a text the player sees as `i18n::tr("...")` where it is shown, or
`i18n::N_("...")` in a table of texts that is passed through `tr()` later
([common/i18n.h](../common/i18n.h)). Then run `python3 tools/i18n_template.py`
to write the template again, and commit it with the change. A text changed in
the code is a new text: the translations of the old one stop being used, and
`--check` names them.
