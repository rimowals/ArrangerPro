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
| `app/` | aplikacja JUCE: MIDI I/O, zegar czasu rzeczywistego, GUI; `arranger_smoke` | core, JUCE 8.0.9 |

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

## Partie klawiatury, OTS i pady

- `Style::ots` — do 4 `OneTouchSetting`, każdy z `PartVoice` dla R1, R2, R3, L
  (wł./wył., bank, program, transpozycja, głośność/panorama/efekty; `-1` = bez zmiany).
  - Yamaha: chunk `OTSc` = 4× `MTrk`; kanał 1 → R1, 2 → R2, 3 → R3, 4 → L (w stylach z OTS
    kanał 4 ma to samo brzmienie we wszystkich OTS — typowe dla lewej ręki). Sysex jest pomijany.
  - Korg Pa50: obiekt STS nieopisany publicznie; rekord brzmienia zawiera `7F 15 6C`, potem
    MSB(+3), LSB(+4), program(+6), głośność(+7), panorama(+8), transpozycja(+10, ze znakiem).
    8 rekordów akompaniamentu, potem bloki po 4 (Upper1–3, Lower) na każde STS.
    Bit 2 drugiego bajtu nagłówka = partia gra (Upper 1 ma go zawsze).
  - Pady w STS Korga: po `<nr STS> 02 01 00 00 00 DE 00 FE 04 …` 4 rekordy po 10 bajtów
    (numer padu, FF, głośność, panorama). Numer = pozycja (od 0) na listach Pa50: dźwięki 0–138,
    potem funkcje (`arranger/KorgPads.h`; zgodność potwierdzona m.in. Equinoxe 5 = Upp1–Low Mute).
- `PadPlayer` (rdzeń) — fraza z pliku MIDI na osobnym kanale, ta sama oś ticków co styl,
  pętla, dopasowanie do akordu (Melody/Root Trans względem CM7), perkusja bez transpozycji.
- Kanały wyjścia: 1–4 partie klawiatury, 5–8 pady, 9–16 style.

## Aplikacja (`app/`)

- `ArrangerController` — jedyny punkt styku z czasem rzeczywistym. Trzyma `StyleEngine`,
  `ChordRecognizer`, porty MIDI. Zegar: `HighResolutionTimer` co 1 ms przelicza czas
  (`Time::getMillisecondCounterHiRes`) na tick (`baseTick + Δms · tempo · PPQ / 60000`)
  i woła `engine.advanceTo()`. Zmiana tempa „przekotwicza” zegar, więc pozycja się nie przesuwa.
- Wątki: timer (1 ms), wątek wejścia MIDI, wątek GUI. Silnik i stan klawiatury chroni
  `engineLock_`; wyjście MIDI — osobny `outLock_` (kolejność: engine → out). Każde sterowanie
  najpierw dociąga silnik do „teraz”, więc zmiany akordu/sekcji działają z dokładnością < 1 ms.
- Wejście: nuty poniżej splitu → zbiór trzymanych klawiszy → rozpoznanie akordu → `setChord`
  (akord zostaje po puszczeniu klawiszy); reszta → przelot na kanał prawej ręki.
- GUI (`MainComponent`, `StyleBrowser`, `SectionPanel`, `MixerPanel`) odpytuje
  `controller.status()` 30×/s; nie ma wywołań zwrotnych z wątków czasu rzeczywistego do GUI
  (poza „Ucz split”, przekazywanym przez `MessageManager::callAsync`).

## Etapy

1. ✅ core + parsery + akordy + transpozycja + sekwencer + `arrtool render`.
2. ✅ Aplikacja JUCE: wybór portów MIDI, split, zegar RT, panel sterowania (Intro/Main/Fill/Ending,
   Sync Start, tempo, mikser partii), przeglądarka stylów.
3. ✅ Partie R1–R3/L, OTS (Yamaha OTSc, Korg STS), multipady (pliki MIDI).
3b. Rejestracje, playlista, pady Yamaha (.pad) i Korg.
4. Edytor stylów.
