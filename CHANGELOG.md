# Changelog

Format oparty na [Keep a Changelog](https://keepachangelog.com/pl/1.1.0/),
wersjonowanie zgodne z [SemVer](https://semver.org/lang/pl/).

## [Unreleased]

## [0.1.0] - 2026-10-31

Etap 1: rdzeń aranżera (biblioteka `arranger_core`, bez zależności od JUCE).

### Dodano
- Wspólny model stylu: sekcje Intro/Main/Fill/Break/Ending A–D, wariacje akordowe,
  8 partii akompaniamentu, rozdzielczość wewnętrzna 1920 PPQ.
- Czytnik stylów Yamaha SFF1/SFF2 (SMF + markery sekcji, CASM: Ctab, Ctb2, Cntt).
- Czytnik banków Korg Pa (kontener KORF, dekompresja OC31, master tracki,
  wariacje akordowe, tempo z obiektu STS); wczytywanie całych folderów `.SET`.
- Rozpoznawanie akordów: 34 typy Yamahy, tryby Fingered, Fingered On Bass, Single Finger.
- Transpozycja nut: NTR (Root Trans/Root Fixed/Guitar), NTT (Bypass, Melody, Chord,
  tablice molowe, Bass On), High Key, limity nut, note/chord mute, RTR.
- `StyleEngine`: sekwencer oparty na tickach — Intro → Main, fill od bieżącej pozycji
  i zmiana Main na kresce taktowej, Break, Ending ze stopem, Sync Start, wyciszanie partii.
- Zapis wyjścia silnika do pliku MIDI (format 0).
- Narzędzie `arrtool`: `info` (struktura stylu) i `render` (styl + progresja akordów → .mid).
- 21 testów jednostkowych (akordy, transpozycja, czytnik Yamaha, silnik).

### Znane ograniczenia
- Korg Pa50 (dane w wersji 0.0) nie zapisuje akordu źródłowego wariacji — przyjmowane
  jest CM7/Cm7/C7 wg tablicy akordów; wymaga weryfikacji na słuch.
- RTR „Pitch Shift” jest realizowany jako ponowne uderzenie nuty (MIDI nie pozwala
  przestroić pojedynczej nuty akordu).
- Tablice gitarowe SFF2 (Guitar NTR) są przybliżane tablicą Chord.
