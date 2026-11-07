# Changelog

Format oparty na [Keep a Changelog](https://keepachangelog.com/pl/1.1.0/),
wersjonowanie zgodne z [SemVer](https://semver.org/lang/pl/).

## [Unreleased]

### Dodano
- „Zapisz jako domyślne” / „Przywróć domyślne” w zakładce „Brzmienia klawiatury”: własne partie
  domyślne dla stylów bez OTS, zapamiętywane w ustawieniach.
- Multipady ze STS stylów Korg Pa50: numer padu, głośność, panorama (`arrtool info` pokazuje
  nazwy). Pady-dźwięki (0–138 wg listy z instrukcji Pa50) grają najbliższym brzmieniem GM/XG,
  pady-funkcje działają: wyciszanie Upper 1–3/Lower i partii stylu, Acc Mute, Drum&Bass,
  Style Up/Down, Prog Up/Down (R1), Perf Up/Down (→ OTS). Wyciszające pady świecą, gdy aktywne.
- **Biblioteka padów** w zakładce „Multi pady”: plik MIDI nazwany numerem padu zastępuje pad
  ze stylu (np. dla dźwięków bez odpowiednika GM).

### Naprawiono
- Korg: STS o nazwie innej niż styl (np. Mutiny → „Vangelis”) nie był wczytywany — styl
  tracił OTS i tempo.

### Zmieniono
- Wczytanie stylu ustawia partie klawiatury z jego OTS 1 (najpierw własne OTS użytkownika);
  styl bez OTS przywraca partie domyślne.
- Przycisk L i kolumna L w „Brzmieniach klawiatury” są na lewo od R1 (kolejność L, R1, R2, R3).

## [0.3.0] - 2026-10-31

### Dodano
- Partie klawiatury **R1, R2, R3, L** (kanały 1–4) z przyciskami wł./wył. i edytorem brzmień
  (program GM, bank MSB/LSB, transpozycja, głośność). Lewa ręka gra partią L niezależnie
  od rozpoznawania akordów; pedał i kółka idą do włączonych partii prawej ręki.
- **OTS 1–4**: odczyt One Touch Settings ze stylów Yamaha (`OTSc`) i STS ze stylów Korg Pa
  (rozpoznany układ binarny: bank GM2, program, głośność, panorama, transpozycja, stan partii).
  **LINK** (Main A–D wybiera OTS 1–4) i **ZAPISZ** (własne OTS dla stylu, zapamiętywane
  w ustawieniach; oznaczone gwiazdką).
- **Multi pady 1–4** (kanały 5–8): fraza z pliku MIDI, jednorazowo lub w pętli, z dopasowaniem
  do akordu (perkusja nie jest transponowana); `PadPlayer` w rdzeniu.
- Intro można uruchomić w trakcie gry — zaczyna się od następnego taktu i wraca do wybranego
  Main (`StyleEngine::selectIntro`).
- Skróty: 5–8 OTS, F1–F4 pady, F5 stop padów; `arrtool info` pokazuje OTS.

### Zmieniono
- Wspólny parser SMF w rdzeniu (style Yamaha, ścieżki OTS, pliki padów).
- Usunięto ustawienia „kanał prawej ręki” i „lewa ręka na wyjście” — zastępują je partie R1–L.

## [0.2.0] - 2026-10-31

Etap 2: aplikacja desktopowa (JUCE 8.0.9) grająca na żywo przez MIDI.

### Dodano
- Aplikacja `ArrangerPro.exe`: wybór wejścia/wyjścia MIDI, opcjonalny MIDI Clock (Start/Stop/F8).
- `ArrangerController`: zegar czasu rzeczywistego 1 ms, podział klawiatury (split + „Ucz split”),
  rozpoznawanie akordów w strefie lewej ręki z pamięcią akordu, przelot prawej ręki
  (i opcjonalnie lewej) na wybrane kanały, Sync Start, tap tempo.
- Przeglądarka stylów: folder (rekurencyjnie), wyszukiwanie, style Yamaha oraz banki Korg
  (każdy styl banku osobno), wczytywanie po zaznaczeniu.
- Panel sekcji Intro/Main/Break/Ending z kolorami stanu (gra / następna / wybrana),
  Auto Fill, Sync Start, Start/Stop; wyświetlacz akordu, sekcji, taktu, uderzeń i tempa.
- Mikser 8 partii: głośność (nadpisuje CC7 stylu) i wyciszanie.
- Przełącznik „Program Change”: Bank Select + Program Change ze stylu dla modułów GM/XG;
  ustawienia brzmień wysyłane od razu po wczytaniu stylu (`StyleEngine::sendSetupNow`).
- Banki Korga GM2 (MSB 120/121) tłumaczone na XG (MSB 127/0); o perkusji decyduje bank,
  nie numer ścieżki. `arrtool render` zapisuje zmiany programów w pliku MIDI.
- Skróty klawiszowe; zapis ustawień w `%APPDATA%\ArrangerPro\ArrangerPro.settings`.
- `arranger_smoke`: test end-to-end kontrolera na prawdziwym porcie MIDI (loopMIDI).
- Rdzeń: `StyleEngine::setPartVolume`, nadpisywanie głośności partii w ustawieniach silnika.

### Zmieniono
- Flagi ostrzeżeń kompilatora dotyczą tylko celów projektu (nie pobranych zależności).

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
