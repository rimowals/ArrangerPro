# ArrangerPro — architektura

Programowy aranżer dla Windows. Wczytuje style Yamaha (SFF1/SFF2/SFF GE) i Korg Pa,
rozpoznaje akordy grane lewą ręką i generuje akompaniament jako **MIDI**.
Dźwięk produkuje zewnętrzny host VST (Cantabile Performer), który dostaje MIDI
przez wirtualny port (loopMIDI / Windows MIDI Services).

```
 klawiatura MIDI ──► [ArrangerPro] ──► port wirtualny ──► Cantabile (VST)
                      │  split / akordy
                      │  silnik stylu
                      └► przelot prawej ręki (Upper) na osobnym kanale
```

## Moduły

| Katalog | Cel | Zależności |
|---|---|---|
| `core/` | biblioteka `arranger_core`: model stylu, parsery, akordy, transpozycja, sekwencer | tylko C++20 STL |
| `tools/arrtool/` | CLI: `info` (zrzut struktury stylu), `render` (styl + progresja akordów → plik .mid) | core |
| `tests/` | testy jednostkowe (własny mini-framework, bez zależności) | core |
| `app/` *(etap 2)* | aplikacja JUCE: MIDI I/O, zegar czasu rzeczywistego, GUI | core, JUCE |

`core` nie zna czasu rzeczywistego ani urządzeń — operuje na tickach.
Dzięki temu cały silnik da się testować offline (render do pliku MIDI).

## Model stylu (`arranger/Style.h`)

Jeden wspólny model dla Yamahy i Korga:

- `Style` → sekcje (`SectionId`: Intro A–D, Main A–D, Fill AA/BB/CC/DD, Break, Ending A–D),
  tempo, metrum, ustawienia startowe kanałów.
- `Section` → jedna lub więcej **wariacji akordowych** (`ChordVariation`)
  i tablica *typ akordu → wariacja*. Yamaha ma zawsze jedną wariację; Korg do 6 (Main) lub 2.
- `ChordVariation` → do 8 partii (`PartRole`: SubRhythm, Rhythm, Bass, Chord1, Chord2, Pad, Phrase1, Phrase2).
- `Part` → nuty (z długością, nie pary on/off), pozostałe zdarzenia (CC, bend…) oraz
  `TransposeRule` (NTR/NTT/High Key/limity/RTR/mute — odpowiednik Ctab/Ctb2).

Wszystkie czasy są normalizowane do **1920 PPQ** (`kPpq`).

Mapowanie Korga: Drum→Rhythm, Perc→SubRhythm, Bass→Bass, Acc1..5→Chord1, Chord2, Pad, Phrase1, Phrase2;
Variation1–4→Main A–D, Intro1–3→Intro A–C, Ending1–3→Ending A–C, Fill1–4→Fill AA–DD, Break→Break.

## Transpozycja (`arranger/Transposer.h`)

Każda partia jest nagrana w *akordzie źródłowym* (Yamaha: zapisany w Ctab, zwykle CMaj7;
Korg: CM7 / Cm7 / C7 zależnie od wariacji). Dla granego akordu:

1. **NTR** — `RootTrans` (przesunięcie o różnicę prymy z regułą High Key) albo `RootFixed`
   (wysokość możliwie blisko oryginału).
2. **NTT** — `Bypass` (tylko przesunięcie), `Melody` (mapowanie stopni skali akordu
   źródłowego na skalę akordu docelowego), `Chord` (mapowanie funkcji w akordzie: 1/3/5/7/9/11/13
   na składniki akordu docelowego), warianty molowe (inna skala źródłowa), `Bass On`.
3. Limity nut (zawijanie oktawowe), note mute / chord mute.
4. **RTR** przy zmianie akordu w trakcie trwania nuty: Stop / Pitch shift / Retrigger.

## Silnik (`arranger/StyleEngine.h`)

Sekwencer oparty na tickach: `advanceTo(tick)` emituje zdarzenia MIDI przez callback.
Maszyna stanów sekcji (zgodnie z zachowaniem aranżerów):

- Start: (Intro →) Main; Sync Start — start pierwszym akordem.
- Zmiana Main w trakcie gry → Fill (od bieżącej pozycji w takcie) → nowy Main od następnego taktu.
- Ending → stop po zakończeniu; Break → powrót do bieżącego Main.
- Zmiana akordu przemapowuje brzmiące nuty wg RTR.

## Rozpoznawanie akordów (`arranger/Chord.h`)

34 typy akordów Yamahy (indeksy jak w Ctab), dopasowanie wzorców po klasach wysokości
z priorytetem dla najniższego dźwięku jako prymy; tryby Fingered, Fingered On Bass,
Single Finger.

## Etapy

1. **(teraz)** core + parsery + akordy + transpozycja + sekwencer + `arrtool render`.
2. Aplikacja JUCE: wybór portów MIDI, split, zegar RT, panel sterowania (Intro/Main/Fill/Ending,
   Sync Start, tempo, mikser partii), przeglądarka stylów.
3. Rejestracje, OTS, multipady, playlista.
4. Edytor stylów.
