# Translating the game: rules

How to translate RAC1's texts into a language the disc lacks, learned while
making [game/pl.po](game/pl.po). [README.md](README.md) says where the files
are and how to unpack, pack and check them; this file says what to write in
them. The rules hold for any language. The glossary at the end is Polish.

## Length

The game draws its texts in fixed boxes and cuts what does not fit; nothing
wraps except the help boxes and the subtitles.

- **Menu rows, list entries and titles are no longer than the English.**
  The pause menu, the Options and Goodies pages, the help topics, the item
  and move names, the skill-point names and the planet names sit in columns
  of fixed width. Count letters, not bytes: `ł` is one letter. "Dziennik
  pomocy" for "Help Log" was drawn as "ziennik pomoc"; it is "Dziennik" now.
- **Shorten by choosing another word, not by cutting one.** Drop what the
  context already says: "Wall jump hint" is "Skok ze ściany", not "Wskazówka
  o skoku ze ściany"; "Press [R1] for help" is "[R1] - pomoc". Abbreviate
  only as a last resort, with a dot ("Rakiety samonaprow.").
- **Banners and prompts stay on one line.** "[triangle] …" prompts, "You
  got the …!" banners and the ammo pickups ("+%d amunicji do …") should be
  about the English length.
- **Keep every `\n` where the English has one.** The Gold Weapons prompts
  and the credits are laid out by them.
- **Magazine covers and posters are short.** Each line of a cover is its own
  text and sits in a fixed spot: keep each line about as long as the
  English, and check that the lines still read in order (Party / at the /
  Hotbot / mansion became "IMPREZA / W / WILLI / HOTBOTA").
- **Help boxes wrap themselves**, so a longer sentence is fine there.
  Keep a subtitle about the English length: it is shown for as long as the
  line is spoken.

## Brackets, numbers and bytes

- **Keep every bracket the English has**, as many of each, moved to where
  the sentence needs them: buttons (`[cross]`, `[lstick]`), colours
  (`[orange]` … `[default]`) and `[xAD]`. `game_text_po.py --check` refuses
  a translation whose brackets differ.
- **A name in `[orange]` stays inside the colour with its ending**:
  `[orange]Swingshota[default]`, not `[orange]Swingshot[default]a`.
- **`%d` stays `%d`**, in the same order.
- **`[xAD]` marks a pause in speech** ("Well[xAD]no."). Keep it where the
  speaker pauses; do not replace it with "...".
- **Spaces at the start or end of a subtitle join it to the line before or
  after.** Keep them.
- **Letter-by-letter words** (`bBbObObMb` in the weapon ads) keep the
  pattern: `b` before each letter and one after the last.

## Names

- **Gadgetron products keep their English names**: Bomb Glove, Swingshot,
  Grindboots, Magneboots, Hologuise, Trespasser, R.Y.N.O., Infobot,
  Heli-Pack, Thruster-Pack, Hydro-Pack and so on. They are brands on the
  boxes and in the shops, and the game shows them in orange. Decline them
  where the language needs it (`Swingshota`, `Infobota`).
- **Moves, places and things that are not products are translated** (Glide,
  Hyper-Strike, Comet-Strike, Kyzil Plateau, Al's Roboshack).
- **One English term, one translation, everywhere.** A move named in the
  help is named the same in the HUD, the missions and the subtitles. Check
  the glossary before inventing a word.
- **Characters keep their names** (Ratchet, Clank, Qwark, Drek, Skid
  McMarx, Big Al is Wielki Al).

## Tone

- **Write what a person would say.** Translate the sentence, not the words:
  "Now we're talkin'!" is "No, to rozumiem!", not "Teraz rozmawiamy!".
- **Help and Gadgetron texts are friendly ads**: second person singular,
  short sentences, an exclamation mark where the English has one.
- **Each character keeps a voice.** Ratchet is a teenager, Clank is formal,
  Qwark is a pompous showman, Drek is a smug executive, the Blarg are
  military, Bob talks like a country mechanic.
- **Keep the jokes**, and when a pun cannot survive, write a joke of your
  own for the same moment. "Rip Ya a New One" stays English, because the
  letters spell R.Y.N.O., followed by the translation.
- **Instructions address the player as "you"** and use the imperative for
  goals ("Find a new ship" → "Znajdź nowy statek").

## Before you commit

```sh
cd port
python3 tools/game_text_po.py pack pl.po lang/game/pl.po
python3 tools/game_text_po.py --check
```

Then play the screens you changed: the pause menu, a help topic, a shop and
one cutscene. A cut word shows only in the game.

## Polish glossary

| English | Polski |
|---|---|
| bolts | śruby |
| Gold Bolt | Złota śruba |
| Gadgetron Vendor | automat Gadgetronu |
| HelpDesk, Help Desk | Pomoc |
| Help Log | Dziennik |
| Items | Przedmioty |
| Skill Points | Punkty umiejętności (menu: Umiejętności) |
| Goodies | Bonusy |
| QuickSelect | Szybki wybór |
| Glide | Szybowanie |
| Boost Jump | Skok z wybiciem (menu: Wybicie) |
| Stretch Jump | Długi skok |
| Hyper-Strike | Hiper-cios |
| Comet-Strike | Cios komety |
| Power Slam | Potężny cios |
| Wall jump | Skok ze ściany |
| Grind Rail | szyna |
| Magne-Strips | Magnetopasy |
| Versa-Targets | Versa-Targety |
| jump slot | szczelina do skakania |
| zipline | tyrolka |
| Sentry-bot | Strażbot |
| Bot-Ports | Porty botów |
| Giant Clank | Gigant Clank |
| O2 Mask | Maska O2 |
| Pilot's Helmet | Hełm pilota |
| Premium / Ultra Nanotech | Nanotech Premium / Ultra |
| Planetbuster | niszczyciel planet |
| Snagglebeast | Snagglebestia |
| sand shark, sand mouse | piaskowy rekin, piaskowa mysz |
| Al's Roboshack, Robo-Shack | Robosklep Ala, Robosklep |
| Chairman Drek | prezes Drek |
| (Supreme) Executive Chairman | (Najwyższy) Prezes Zarządu |
| the Blarg; Blargian | Blargowie; blargański |
| Hoverboard | Hoverboard (in names), hoverboard |
| first person | z oczu |

Polish letters are written normally (ą ć ę ł ń ó ś ź ż). The game's font
lacks them, so the port draws each one from the plain letter and a mark;
they count as one letter for length.
