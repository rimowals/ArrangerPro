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
| 2 | aplikacja JUCE: MIDI I/O, zegar RT, panel sterowania | ⏳ |
| 3 | rejestracje, OTS, multipady, playlista | — |
| 4 | edytor stylów | — |

## Struktura

```
core/          biblioteka arranger_core (tylko STL)
  include/arranger/   publiczne API: Style, Chord, ChordRecognizer, Transposer, StyleEngine, StyleLoader, SmfWriter
  src/                implementacja, czytniki Yamaha/Korg
tools/arrtool/ narzędzie CLI
tests/         testy jednostkowe
docs/          dokumentacja
```

## Budowanie

Wymaga Visual Studio 2026 z obsługą C++ (CMake jest w nim wbudowany).

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
