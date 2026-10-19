# Translations of the port

The port's own words in other languages: the rows and values of the Video
options page it adds to the game's menus, and the message boxes a player may
see when the game cannot start. The game's own texts come from your disc and
follow the game's Language page; nothing here changes them.

Each language is one [gettext](https://www.gnu.org/software/gettext/manual/html_node/PO-Files.html)
file, `<code>.po`, in this folder. A translation is only text: no code to
edit, and any editor works, a plain text editor or a PO editor such as
[Poedit](https://poedit.net/). Every `.po` file here is built into the
program.

| File | |
|---|---|
| [openrac.pot](openrac.pot) | the template: every English text, empty. Generated from the code; do not edit it |
| [pl.po](pl.po) | Polski |

## Translating

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

## Trying a translation without building

Put the `.po` file in a `lang` folder beside the port's settings file:
`settings/lang/` in the port's data folder (`~/.local/share/openrac` on Linux,
`~/Library/Application Support/OpenRAC` on macOS), or beside the file
`--settings` names; the launcher passes its own, and the log's first lines
say which. The port reads every `.po` there at start, in place
of a built-in one of the same language. Then choose it on the Language row of
the Video options page, or set `language = <code>` in the settings file.
`auto` (System) picks the system's language when there is a translation for
it. The log says which language the port uses and which files it read.

## Checking

```sh
cd port
python3 tools/i18n_template.py --check   # the template is the code's; each .po's texts and {} match it
ctest --preset release -R i18n           # the files read, and the Video page's texts fit
```

The first says how many texts each language still lacks.

## For contributors: a new text in the code

Write a text the player sees as `i18n::tr("...")` where it is shown, or
`i18n::N_("...")` in a table of texts that is passed through `tr()` later
([common/i18n.h](../common/i18n.h)). Then run `python3 tools/i18n_template.py`
to write the template again, and commit it with the change. A text changed in
the code is a new text: the translations of the old one stop being used, and
`--check` names them.
