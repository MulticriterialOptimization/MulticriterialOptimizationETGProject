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
4. wyznacza start i koniec: pojedyncze zadanie → `start = max(dane gotowe, procesor wolny)`,
   `finish = start + czas`; common → każdy kawałek osobno, `finish = max` kawałków (patrz §4.2);
5. aktualizuje stan (`markProcUsed`): liczniki użyć, `freeAt`, `lastFinish`, spalone wyspecjalizowane.

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
| `Fastest`/`AllocFastest` | `max times[t][p]/k` (najwolniejszy kawałek — kawałki idą równolegle) |
| `MinTS` | czas(S) × koszt(S) |
| `AllocLFU` | `Σ peUseCount[p]` po `S` |
| `AllocIdle` | `min lastFinish[p]` po `S` |
| `AllocSamePred` | reprezentant k=1: procesor rodzica (fallback `Cheapest`) |

Wygrywa podzbiór z najmniejszym score. Model czasu/kosztu to `1/k` z dokumentacji
(`commonExecTime`, `commonExecCost`): każdy z `k` procesorów robi 1/k pracy **równolegle**.

### 4.2 Czy równoległość jest brana pod uwagę? TAK — w tych miejscach:

- **Kawałki liczą się równolegle, każdy procesor startuje osobno.** W `evaluateIndividual`:
  `pieceStart(p) = max(dataReady, freeAt[p])`, `pieceFinish(p) = pieceStart(p) + times[t][p]/k`.
  Zajęty procesor opóźnia **tylko swój kawałek** — reszta liczy w tym czasie swoje.
- **Koniec zadania = najwolniejszy kawałek.** `finish(t) = max_p pieceFinish(p)` — przykład:
  a=10, b=20, k=2 → kawałki 5 i 10, oba wolne od zera → zadanie kończy się po **10** (nie 15).
- **Każdy procesor zajęty tylko przez swój kawałek.** `markProcUsed` ustawia `freeAt[p] =
  pieceFinish(p)` per procesor — kto skończył swój kawałek wcześniej, wcześniej jest wolny
  do innych zadań (wyspecjalizowane są spalane).
- **Uniwersalny = kolejka sekwencyjna.** Dwa zadania na tym samym uniwersalnym procesorze
  wykonują się po kolei (drugie czeka na `freeAt`), niezależnie od zależności w grafie.
- **Zadania niezależne biegną równolegle „same z siebie".** Każdy procesor ma własny `freeAt`,
  więc T1 na P0 i T2 na P1 mogą się nakładać w czasie — makespan to `max finish`, nie suma.
- **Komunikacja z common jest dzielona i czeka na komplet.** Wynik powstaje w `k` kawałkach
  i jest kompletny dopiero, gdy skończy się **całe zadanie** (`finish = max kawałków`) — dopiero
  wtedy każdy procesor z `S` wysyła swoje `data/k` (`predecessorsReadyTime` używa `finishTime`
  poprzednika), a **potomek startuje dopiero z kompletem danych** (max po wszystkich transferach).
  Kawałek, który zostaje na procesorze wykonującym następne zadanie, jest lokalny (za darmo).

### 4.3 Model równoległy `max` udziałów — dlaczego `k > 1` się opłaca

Poprzednia wersja kodu liczyła `czas(S) = Σ times[t][p]/k` (suma udziałów = średnia) — czyli
kawałki de facto wykonywały się „po sobie" i rozbicie na wiele procesorów nigdy nie skracało
czasu. **Zespół zmienił model na równoległy** (decyzja projektowa, wdrożona w kodzie):

```
czas(S) = max_p ( times[t][p] / k )      // koniec = najwolniejszy kawałek
koszt(S) = Σ_p ( costs[t][p] / k )       // koszt bez zmian: suma udziałów
```

Konsekwencje (przykłady):

- **Identyczne procesory: realny speedup ~k×.** Dwa procesory t=20, c=5:
  `{P0}`: czas 20, koszt 5; `{P0,P1}`: czas max(10,10) = **10**, koszt **5** —
  dwa razy szybciej za tę samą cenę → `Fastest` faktycznie wybiera `k=2`.
- **Różne procesory: wolniejszy ogranicza.** a=10, b=20, k=2 → kawałki 5 i 10 →
  czas **10** (czekamy na wolniejszego), koszt = połowa a + połowa b.
- **Koszt dalej faworyzuje singla.** `Σ costs/k` to średnia kosztów, więc czysto kosztowo
  najtańszy pojedynczy procesor pozostaje optimum — `Cheapest` zwykle wybierze `k=1`.
  To zdrowy trade-off: rozbijanie na wiele procesorów **kupuje czas** (ważne przy ciasnym
  `Tmax`), a nie obniża kosztu.

Dzięki temu geny naprawdę różnicują rozmiar zbioru: `Fastest`/`MinTS` potrafią wziąć duże `S`,
`Cheapest` małe — i GA balansuje między nimi pod ograniczeniem `Tmax`.

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
| Kiedy zadanie startuje | pojedyncze: `max(dataReady, freeAt[p])`; common: per kawałek `pieceStart(p)` w `evaluateIndividual` |
| Zajętość procesorów | `markProcUsed` / `markProcsUsed` (`freeAt`, `burnedSpec`) |
