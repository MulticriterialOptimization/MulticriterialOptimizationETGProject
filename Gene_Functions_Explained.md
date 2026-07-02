# Notatka: jak naprawdę działają funkcje genowe (PE / CLS)

> Cel: odpowiedź na wątpliwość „nie jestem pewny, jak działają te wszystkie funkcje genowe,
> zwłaszcza dla tasków, które mogą być robione równolegle (CDT/CGT), i czy to jest brane pod uwagę".
> Wszystko poniżej odnosi się do kodu w `src/evaluator.cpp` (nazwy funkcji podane wprost).

---

## 1. Ogólny mechanizm: gen = reguła decyzyjna, nie decyzja

Osobnik NIE mówi „T3 → P2". Osobnik mówi „dla T3 użyj reguły *AllocCheapest*".
Konkretny procesor wychodzi dopiero w trakcie budowy harmonogramu, bo reguła patrzy na
**bieżący stan** (kto już kupiony, kto zajęty, kto bezczynny). Evaluator (`evaluateIndividual`)
idzie po zadaniach w porządku topologicznym i dla każdego zadania `t`:

1. buduje pulę kandydatów: `allowed[t]` ∩ dostępne (`availableCandidates`) —
   procesor wyspecjalizowany, który już wykonał zadanie, odpada (`burnedSpec`);
2. stosuje regułę `PeGene[t]` → wybór procesora / zbioru procesorów (`pickProcs`);
3. liczy czas gotowości danych od poprzedników, w razie potrzeby wybierając kanały
   regułą `ClsGene[t]` (`predecessorsReadyTime` → `pickChannel`);
4. wyznacza `start = max(dane gotowe, procesory wolne)` i `finish = start + czas wykonania`;
5. aktualizuje stan (`markProcsUsed`): liczniki użyć, `freeAt`, `lastFinish`, spalone wyspecjalizowane.

## 2. Geny PE — zadania pojedyncze (GT / DT / UT), funkcja `pickSingleProc`

Grupa „Allocated" najpierw zawęża pulę do procesorów **już użytych** w tym harmonogramie
(`filterUsed`); jeśli żaden użyty nie jest legalny — gen degraduje się do globalnego `Cheapest`.

| Gen | Co liczy (score, min = wygrywa) | Intuicja |
|---|---|---|
| `AllocCheapest` / `Cheapest` | `costs[t][p]` + `buyCost` przy pierwszym użyciu (`procExecCost`) | najtańsze wykonanie |
| `AllocFastest` / `Fastest` | `times[t][p]` | najszybsze wykonanie |
| `MinTS` | `times[t][p] × costs[t][p]` | kompromis czas/koszt |
| `AllocLFU` | `peUseCount[p]` | równoważenie obciążenia |
| `AllocIdle` | `lastFinish[p]` (mniejszy = dłużej bezczynny) | wykorzystaj „stojący" zasób |
| `AllocSamePred` | procesor rodzica w drzewie rozpinającym (`pickSameAsPred`); jak niedostępny → fallback `Cheapest` | lokalność = darmowa komunikacja |

## 3. Geny CLS — wybór kanału, funkcja `pickChannel`

Reguła brana z węzła **odbiorcy** danych; stosowana osobno dla każdej pary (nadawca, odbiorca)
z `data > 0` na różnych procesorach. „Allocated" = kanały, do których oba końce są już podłączone
(zapłacony `connectCost`); jak brak — degradacja do `ClsCheapest`.

| Gen | Score |
|---|---|
| `ClsAllocCheapest` / `ClsCheapest` | `connectCost` + koszt krańcowy nowych podłączeń |
| `ClsAllocFastest` / `ClsHighestBw` | `−bandwidth` (max przepustowość) |
| `ClsAllocLFU` / `ClsLFU` | `channelUseCount[c]` |
| `ClsAllocIdle` | `channelLastFinish[c]` (mniejszy = dłużej bezczynny) |

## 4. Zadania równoległe (CDT / CGT) — sedno wątpliwości

### 4.1 Jak wybierany jest zbiór `S` (funkcja `pickCommonProcs`)

Liczba procesorów `k` **nie jest w pliku** — wybiera ją algorytm. Kod enumeruje **wszystkie
niepuste podzbiory** puli kandydatów (pętla po maskach bitowych, do `2^|pool|−1`) i każdy
podzbiór ocenia funkcją `scoreCommonSubset` według aktywnego genu:

| Gen | Score podzbioru `S` (k = \|S\|) |
|---|---|
| `Cheapest`/`AllocCheapest` | `Σ costs[t][p]/k` + buyCosty nowych procesorów |
| `Fastest`/`AllocFastest` | `Σ times[t][p]/k` |
| `MinTS` | czas(S) × koszt(S) |
| `AllocLFU` | `Σ peUseCount[p]` po `S` |
| `AllocIdle` | `min lastFinish[p]` po `S` |
| `AllocSamePred` | reprezentant k=1: procesor rodzica (fallback `Cheapest`) |

Wygrywa podzbiór z najmniejszym score. Model czasu/kosztu to `1/k` z dokumentacji
(`commonExecTime`, `commonExecCost`): każdy z `k` procesorów robi 1/k pracy.

### 4.2 Czy równoległość jest brana pod uwagę? TAK — w tych miejscach:

- **Start czeka na wszystkich.** `start(t) = max(dataReady, max freeAt[p] po p∈S)`
  (`procReadyTime`) — zadanie common nie ruszy, dopóki *każdy* procesor z `S` nie będzie wolny.
- **Wszyscy z S są zajęci do `finish`.** `markProcsUsed` ustawia `freeAt[p] = finish` dla
  uniwersalnych i spala wyspecjalizowane — w trakcie zadania common żaden z jego procesorów
  nie robi nic innego.
- **Uniwersalny = kolejka sekwencyjna.** Dwa zadania na tym samym uniwersalnym procesorze
  wykonują się po kolei (drugie czeka na `freeAt`), niezależnie od zależności w grafie.
- **Zadania niezależne biegną równolegle „same z siebie".** Każdy procesor ma własny `freeAt`,
  więc T1 na P0 i T2 na P1 mogą się nakładać w czasie — makespan to `max finish`, nie suma.
- **Komunikacja z common jest dzielona.** Wynik powstaje w `k` kawałkach: każdy procesor z `S`
  wysyła `data/k` osobno (`predecessorsReadyTime`, `piece = data / kProd`); kawałek, który
  zostaje na procesorze wykonującym następne zadanie, jest lokalny (za darmo, w zerowym czasie).

### 4.3 WAŻNA obserwacja: przy obecnym modelu `k > 1` praktycznie nigdy nie wygrywa

To trzeba rozumieć (i warto zapytać prowadzącego). Wzór z dokumentacji
(`Input_format.md`) mówi: `czas(S) = Σ times[t][p]/k` — to jest **średnia arytmetyczna**
czasów wybranych procesorów. A średnia nigdy nie jest mniejsza od najmniejszego elementu:

- pula: P0 (t=30, c=3), P1 (t=15, c=2)
- `{P1}`: czas 15, koszt 2
- `{P0,P1}`: czas (30+15)/2 = **22.5**, koszt (3+2)/2 = **2.5**

Czyli: `Fastest` wybierze `{P1}` (15 < 22.5), `Cheapest` wybierze `{P1}` (2 < 2.5).
Dołożenie drugiego procesora **zawsze** pogarsza (lub wyrównuje) i czas, i koszt względem
najlepszego singla — a buyCost nowych procesorów dodatkowo karze większe zbiory. Analogicznie
LFU (suma rośnie z rozmiarem) i Idle (singleton z najbardziej bezczynnym procesorem remisuje
z każdym nadzbiorem, a przy remisie wygrywa pierwszy w kolejności enumeracji, czyli mniejszy).

**Wniosek:** mechanizm wyboru podzbioru działa poprawnie względem udokumentowanego modelu,
ale matematyczną konsekwencją modelu „suma udziałów = średnia" jest to, że optimum to zawsze
`k = 1`. Zadania CDT/CGT *mogą* dostać wiele procesorów, ale się to nie opłaca.
Gdyby intencja była taka, że praca dzieli się **równolegle w czasie**
(czas = `max_p times[t][p]/k`, czyli realny speedup ~k×), to `k > 1` stawałoby się opłacalne
i geny zaczęłyby faktycznie różnicować rozmiar zbioru. To pytanie o **semantykę modelu do
prowadzącego**, nie bug kodu — kod wiernie realizuje `Input_format.md`.

## 5. Czego model NIE uwzględnia (świadome uproszczenia)

- **Kanały nie mają kolejki** — dwa transfery w tym samym czasie na jednym kanale nie blokują
  się nawzajem; `channelUseCount`/`channelLastFinish` służą tylko heurystykom LFU/Idle.
- **Transfery od różnych poprzedników liczą się niezależnie** (równolegle); czas gotowości
  danych to max po krawędziach, nie suma.
- **`data/k` to dzielenie całkowite** (`piece = data / kProd`) — reszta z dzielenia znika;
  przy dużych wolumenach pomijalne, przy małych lekko zaniża czas transferu.
- **Brak kary za brak kanału** — zakładamy instancje poprawne (legalny kanał istnieje);
  patrz założenie w `ETG_GA_Design_v2.md` §2.5.

## 6. Szybka mapa kodu

| Pytanie | Funkcja w `evaluator.cpp` |
|---|---|
| Kto może wykonać zadanie? | `availableCandidates` (+ `prep.allowed`) |
| Jak działa grupa „Allocated"? | `filterUsed` + degradacja do `Cheapest` |
| Wybór procesora (zwykłe zadanie) | `pickSingleProc` |
| Wybór zbioru (CDT/CGT) | `pickCommonProcs` → `scoreCommonSubset` |
| Czas/koszt common `1/k` | `commonExecTime`, `commonExecCost` |
| Wybór kanału | `pickChannel` (gen odbiorcy) |
| Podział danych z common | `predecessorsReadyTime` (`piece = data/kProd`) |
| Kiedy zadanie startuje | `procReadyTime` + `max(dataReady, procReady)` w `evaluateIndividual` |
| Zajętość procesorów | `markProcsUsed` (`freeAt`, `burnedSpec`) |
