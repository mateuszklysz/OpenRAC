# Translations

Two kinds of text can be translated, each as one
[gettext](https://www.gnu.org/software/gettext/manual/html_node/PO-Files.html)
file a language, edited with any text editor or a PO editor such as
[Poedit](https://poedit.net/). Nobody edits code.

- **The game's texts**: menus, the HUD, help boxes, banners and subtitles,
  every one of them. Their English comes from your disc, so the files with it
  are made on your computer and stay there; a translation into a language the
  disc lacks is built into the port from [game/](game), which holds only the
  translation (below, [The game's texts](#the-games-texts)).
- **The port's own words**: the rows and values of the Video options page it
  adds to the game's menus, the names of its languages, and the message boxes a player may see when the
  game cannot start. These files are in this folder and built into the
  program.

Both follow the language chosen on the game's own Options → Language page,
where the port adds its languages after the disc's (the settings'
`language`; `auto` picks the system's language when there is a translation
for it). [TRANSLATING.md](TRANSLATING.md) has the rules for writing a
translation of the game.

## The game's texts

At its first start the port reads every text of your disc and writes it to
`settings/lang/game/` in the launcher's install folder, beside the games and
the memory cards (`~/OpenRAC` unless you chose another; the log's first lines
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
game's Language page and the game shows your words; a text left empty stays English.
Editing `fr.po` changes the French the same way. Delete `game.pot` and the
port writes the files again at its next start (your own `<code>.po` stays).

### The translations built into the port

[game/](game) holds the port's own translations of the game's texts, one
`<code>.po` a language, without the English: each `msgid` is the text's key,
its id and the
[CRC-32](https://en.wikipedia.org/wiki/Cyclic_redundancy_check) of its
English (`1000/2ec6618e`), so the file translates every copy of the game and
quotes none. A file in your `settings/lang/game/` folder for the same language
comes first; a text it leaves empty comes from the built-in one.

| File | |
|---|---|
| [game/pl.po](game/pl.po) | Polski: every text of `all_text` (menus, the HUD, missions, help, goodies) |

To edit one beside the English, make it a file of the kind above from your
`game.pot`, edit it, and turn it back:

```sh
cd port
python3 tools/game_text_po.py unpack ~/OpenRAC/settings/lang/game/game.pot lang/game/pl.po pl.po
# edit pl.po in a PO editor; put it in settings/lang/game/ to try it in the game
python3 tools/game_text_po.py pack pl.po lang/game/pl.po
python3 tools/game_text_po.py --check
```

Commit only `lang/game/<code>.po`; the unpacked file has your disc's English.

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
  on). The port draws Polish letters (ą ć ę ł ń ó ś ź ż) from the plain letter
  and a mark; other letters are drawn plain. Write them normally.
- The game itself runs in English underneath while one of the port's
  languages is chosen; choosing a disc language on the same page shows that
  language from the disc instead.

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
   its name in its own words, the way the Language page lists it:

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
`settings/lang/` in the launcher's install folder (`~/OpenRAC` unless you
chose another), or beside the file `--settings` names; the log's first lines
say which. The port reads every `.po` there at start, in place
of a built-in one of the same language. Then choose it on the game's
Options → Language page, or set `language = <code>` in the settings file.
`auto` (System) picks the system's language when there is a translation for
it. The log says which language the port uses and which files it read.

### Checking

```sh
cd port
python3 tools/i18n_template.py --check   # the template is the code's; each .po's texts and {} match it
python3 tools/game_text_po.py --check    # each lang/game/*.po's keys and brackets
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
