# ArrangerPro

Programowy aranżer dla Windows (C++20). Wczytuje style **Yamaha SFF1/SFF2** i **Korg Pa**,
rozpoznaje akordy grane lewą ręką i generuje akompaniament jako **MIDI** dla hosta VST
(np. Cantabile Performer, przez wirtualny port typu loopMIDI).

- Architektura: [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md)
- Historia zmian: [CHANGELOG.md](CHANGELOG.md)

## Status

| Etap | Zakres | Stan |
|---|---|---|
| 1 | rdzeń: parsery stylów, akordy, transpozycja, silnik sekcji, `arrtool` | ✅ |
| 2 | aplikacja JUCE: MIDI I/O, zegar RT, panel sterowania | ✅ |
| 3 | partie R1–R3/L, OTS, multipady | ✅ |
| 3b | rejestracje, playlista | — |
| 4 | edytor stylów | — |

## Aplikacja

`build\app\ArrangerProApp_artefacts\Debug\ArrangerPro.exe`

1. Zainstaluj [loopMIDI](https://www.tobias-erichsen.de/software/loopmidi.html) i utwórz port.
2. W ArrangerPro: **MIDI In** = klawiatura, **MIDI Out** = port loopMIDI.
3. W Cantabile dodaj port loopMIDI jako wejście MIDI i rozprowadź kanały:

   | Kanał | Partia |
   |---|---|
   | 1, 2, 3 | prawa ręka: R1, R2, R3 |
   | 4 | lewa ręka: L |
   | 5–8 | multi pady 1–4 |
   | 9 | SubRhythm (perkusja dodatkowa) |
   | 10 | Rhythm (perkusja) |
   | 11 | Bass |
   | 12–14 | Chord1, Chord2, Pad |
   | 15–16 | Phrase1, Phrase2 |

4. Opcjonalnie włącz **MIDI Clock**, by efekty w Cantabile szły w tempie stylu.

Przełącznik **Program Change** (domyślnie włączony) wysyła ze stylu Bank Select i Program Change —
potrzebne dla multitimbralnych modułów GM/XG (np. S-YXG50 jako VST w Cantabile, wszystkie kanały
na jednej instancji). Wyłącz go, gdy brzmienia każdej partii wybierasz sam w Cantabile.
Banki Korga (GM2: MSB 120 perkusja, 121 melodyczne) są tłumaczone na XG (MSB 127 / 0).

**Partie i OTS.** Przyciski R1/R2/R3/L włączają partie; brzmienia ustawiasz w zakładce
„Brzmienia klawiatury”. OTS 1–4 przywołują ustawienia partii zapisane w stylu (Yamaha OTS,
Korg STS). LINK — wybór Main A–D przełącza też OTS 1–4. ZAPISZ, potem 1–4 — zapamiętuje bieżące
partie jako własne OTS tego stylu (gwiazdka przy numerze).

**Multi pady.** W zakładce „Multi pady” przypisz każdemu padowi plik MIDI (bierzemy kanał
z największą liczbą nut; kanał 10 = perkusja), ustaw Pętlę i Dopasowanie do akordu
(fraza nagrana w C). Pad uruchamia i zatrzymuje ten sam przycisk.

| Skrót | Akcja |
|---|---|
| Spacja | Start / Stop |
| S | Sync Start |
| 1–4 | Main A–D (z fillem, gdy Auto Fill) |
| Q W E R | Intro A–D (przed startem: uzbrojenie; w trakcie gry: od następnego taktu) |
| 5–8 | OTS 1–4 |
| F1–F4 / F5 | multi pad 1–4 / stop padów |
| Z X C V | Ending A–D |
| B | Break |
| F | Auto Fill wł./wył. |
| + / − / T | tempo w górę / w dół / tap |
| ↑ / ↓ | poprzedni / następny styl |

Test end-to-end na porcie loopMIDI:

```
build\app\arranger_smoke_artefacts\Debug\arranger_smoke.exe <styl.sty> ["loopMIDI Port"]
```

## Struktura

```
app/           aplikacja JUCE (ArrangerController, okno, panele) + test dymny
core/          biblioteka arranger_core (tylko STL)
  include/arranger/   publiczne API: Style, Chord, ChordRecognizer, Transposer, StyleEngine, StyleLoader, SmfWriter
  src/                implementacja, czytniki Yamaha/Korg
tools/arrtool/ narzędzie CLI
tests/         testy jednostkowe
docs/          dokumentacja
```

## Budowanie

Wymaga Visual Studio 2026 z obsługą C++ (CMake jest w nim wbudowany). JUCE 8.0.9 jest pobierane
automatycznie przy pierwszej konfiguracji; bez aplikacji: `cmake --preset vs2026 -DARRANGER_BUILD_APP=OFF`.

```
cmake --preset vs2026
cmake --build --preset debug
ctest --preset debug
```

## arrtool

```
arrtool info   <styl.sty | bank Korga .STY | folder .SET> [--detail]
arrtool render <ścieżka> [--style N] --song "C | Am | @B F | G7 !break | C" [--intro A] [--ending A] [--tempo BPM] [--out plik.mid]
```

Skrypt `--song`: takty rozdzielone `|`; w takcie jeden lub więcej akordów (dzielą takt po równo),
`@A`..`@D` wybiera Main, `!break` uruchamia Break, `-` powtarza poprzedni akord.

## Licencja

Projekt prywatny, do użytku niekomercyjnego.
