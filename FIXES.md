# Poprawki solvera GA — dla review

## ETAP 4 — jedna wersja: extended staje się jedyną warstwą ewolucyjną

Zakres: świadoma decyzja zespołu — usunięcie dawnego wariantu bazowego; **algorytm extended
bez zmian**, tylko czyszczenie i ujednolicenie nazw pod oddanie projektu.

- **`src/ga.h` / `src/ga.cpp`:** usunięte `runGa` (stara pętla tournament+elityzm), `mutate`
  (probabilistyczne per `Pm`), `tournamentPick` oraz pola `GaParams`: `useExtendedScheme`,
  `populationSize`, `generations`, `eliteCount`, `tournamentSize`, `crossoverRate`, `mutationRate`.
- **Ujednolicenie nazw** (kod + testy + dokumenty): `runGaExtended` → **`runGa`**,
  `validateExtendedParams` → **`validateGaParams`** — jedna spójna wersja bez przyrostków.
- **`src/main.cpp`:** usunięte flagi `--scheme`, `--pop`, `--generations`; zostają
  `--tmax --seed --lambda --alpha --beta --gamma --delta --rank-pressure --no-improve --max-gen`.
- **Testy:** `test_ga.cpp` bez `useExtendedScheme`; test determinizmu w `test_evaluator.cpp`
  przepisany na parametry extended (`alpha` zamiast `populationSize`).
- **Dokumentacja:** `ETG_GA_Design_v2.md` — §9 oznaczona jako zapis historyczny (poza §9.3,
  które obowiązuje), §1/§10/§11/§13/§15 zaktualizowane na jedyną wersję.
  Wcześniejsze wpisy w tym pliku (etapy 1–3) opisują historyczne nazwy sprzed ujednolicenia.

---

## ETAP 3 — równoległy model zadań common (CDT/CGT)

Zakres: zmiana semantyki czasu zadań common z sekwencyjnej na równoległą (decyzja zespołu).

### 3.1 Czas common = max udziałów (było: suma = wykonanie „po sobie")
**Plik:** `src/evaluator.cpp` (`commonExecTime`, `evaluateIndividual`)
**Co:**
- `commonExecTime`: `Σ times[t][p]/k` → **`max times[t][p]/k`** (kawałki idą równolegle,
  zadanie kończy najwolniejszy kawałek); używane też w scoringu podzbiorów (`scoreCommonSubset`),
- harmonogramowanie per procesor (model B): `pieceStart(p) = max(dataReady, freeAt[p])`,
  `pieceFinish(p) = pieceStart(p) + times[t][p]/k`; `start = min pieceStart`,
  `finish = max pieceFinish`; zajęty procesor opóźnia tylko swój kawałek,
- `markProcUsed` (nowy, per procesor): `freeAt[p] = pieceFinish(p)` — procesor, który skończył
  swój kawałek wcześniej, wcześniej jest wolny,
- komunikacja bez zmian w kodzie, ale semantyka potwierdzona: wysyłka `data/k` rusza po
  zakończeniu **całego** zadania (`finishTime` = max kawałków), potomek startuje z kompletem
  danych (max po wszystkich transferach). Działa dla dowolnego `k ≥ 1`.
**Dlaczego:** stary wzór sumował udziały, czyli kawałki wykonywały się de facto sekwencyjnie —
`k > 1` nigdy nie skracało czasu i sens CDT/CGT (równoległość) ginął. Po zmianie: 2 identyczne
procesory → 2× szybciej za ten sam koszt; koszt (`Σ costs/k`) bez zmian.

### 3.2 Testy
**Plik:** `tests/test_evaluator.cpp`
- `test_common_parallel_shares_halve_time`: CGT na 2 identycznych proc. (t=20) → makespan 10, koszt 5,
- `test_common_piece_waits_for_busy_proc`: zajęty procesor opóźnia tylko swój kawałek
  (makespan 9; stary model dałby 14).

### 3.3 Dokumentacja
`ETG_GA_Design_v2.md` §2.4/§6.4/§6.5, `Input_format.md`, `ETG_concept.md`, komentarz w `src/etg.h`,
`Gene_Functions_Explained.md` §4 (obserwacja „k>1 się nie opłaca" zastąpiona opisem nowego modelu).

---

## ETAP 2 — subtree crossover (§9.3) + schemat rozszerzony extended (§13)

Zakres: wdrożenie decyzji z review PR #6. Evaluator (§6) nieruszany.

### 2.1 Subtree crossover jako główny operator
**Plik:** `src/ga.cpp` (`subtreeCrossover`, `inSubtree`)
**Co:** dziecko = kopia rodzica A; losowany węzeł `r`; geny **całego poddrzewa zakorzenionego
w `r`** (w drzewie rozpinającym, po `parent[]`) nadpisane z rodzica B. Używany w `runGa`
(zamiast dotychczasowego uniform per węzeł) i w `runGaExtended`. 2 rodziców → 1 dziecko.
**Dlaczego:** decyzja z review — „skopiuj tę samą część drzewa" ze slajdu = wymiana poddrzewa;
kształt drzewa wspólny dla wszystkich osobników, więc operacja zawsze bezpieczna.

### 2.2 Rozszerzony schemat ewolucyjny — extended (§13)
**Pliki:** `src/ga.h`, `src/ga.cpp` (`runGaExtended` + pomocnicze)
**Co:**
- `POP = max(2, round(alpha · numTasks · tau))`, `tau` = `countPeTypes` (liczba różnych `typeFlag`),
- frakcje rozłączne: `nCross = round(beta·POP)`, `nMut = round(gamma·POP)`,
  `nClone = POP − nCross − nMut` (reszta wchłania zaokrąglenia; zawsze ≥ 1 — elita),
- **rank selection liniowa**: `linearRankProbs` → `p(i) = (1/POP)(sp − (2sp−2)·i/(POP−1))`,
  wspólna dla crossoveru, mutacji i klonowania,
- **mutacja jako osobny kanał**: `mutateForce` (klon wybranego + pewna podmiana genu w losowym
  węźle; PE albo CLS 50/50, redraw do skutku),
- **dynamiczny stop**: brak poprawy best przez `noImproveLimit` pokoleń (+ bezpiecznik `maxGenerations`),
- walidacja: `beta+gamma+delta = 1`, zakresy `(0,1)`, `alpha > 0`, `sp ∈ [1,2]`
  (`validateExtendedParams`, rzuca `invalid_argument` → exit 1 w main).
**Dlaczego:** §13 planu / slajd „Alpha, Beta, Gamma schema".

### 2.3 CLI
**Plik:** `src/main.cpp`
**Co:** `--scheme basic|extended` (domyślnie basic — pełna kompatybilność wstecz),
`--alpha --beta --gamma --delta --rank-pressure --no-improve --max-gen`; wypis parametrów
schematu i powodu zatrzymania („stopped: no improvement").

### 2.4 Testy
**Plik:** `tests/test_ga.cpp` (+ wpis w `CMakeLists.txt`)
**Co:** `countPeTypes` (1 vs 2 typy), własności `linearRankProbs` (suma=1, `p(0)=sp/POP`,
monotoniczność), walidacja parametrów (zły `beta`, zły `sp` → wyjątek), determinizm
`runGaExtended` dla stałego seeda + znalezienie jedynego dopuszczalnego harmonogramu (koszt 48),
dynamiczny stop (dokładnie `noImproveLimit` pokoleń na instancji bez możliwej poprawy).

### 2.5 Dokumentacja
- `ETG_GA_Design_v2.md`: statusy §1/§9.3/§11/§13/§15 zaktualizowane na „zrealizowane".
- **`Gene_Functions_Explained.md` (nowy):** wyjaśnienie działania wszystkich genów PE/CLS
  z mapą kodu, szczegółowo dla CDT/CGT (wybór podzbioru, model 1/k, komunikacja data/k)
  + **istotna obserwacja**: przy udokumentowanym modelu `czas = Σ t/k` (średnia) podzbiór
  `k > 1` nigdy nie wygrywa z najlepszym singlem — pytanie o semantykę do prowadzącego.

---

## ETAP 1 — zgodność ze specyfikacją + pierwszy test

Zakres: naprawa niezgodności implementacji z `ETG_GA_Design_v2.md` + pierwszy test nowego solvera.
Bez zmian w evaluatorze poza wymienionymi. Nie ruszano parsera ani starego `gp_tree`.

## 1. Wagi genów wpięte (było: uniform)
**Plik:** `src/genes.cpp`
**Co:** `randomPeGene`/`randomClsGene` losują teraz wg wag z §4.3 (`std::discrete_distribution`)
zamiast `uniform_int_distribution`. Wagi spłaszczone z dwupoziomowego rozkładu ze slajdu
(np. `AllocCheapest = 60% * 20% = 12`), tablice `kPeWeights` (8) i `kClsWeights` (7),
pilnowane `static_assert`-em względem `enum ...::COUNT`.
**Dlaczego:** spec §4.3 wymaga konkretnych prawdopodobieństw; uniform dawał zły rozkład
w inicjalizacji i mutacji.

## 2. Odwrócony kierunek „idle longest" (3 miejsca)
**Plik:** `src/evaluator.cpp`
**Co:** zamiana `score = -lastFinish` na `score = lastFinish`:
- `pickChannel` → `ClsGene::ClsAllocIdle`,
- `pickSingleProc` → `PeGene::AllocIdle`,
- `scoreCommonSubset` → `PeGene::AllocIdle` (score podzbioru = jego najmniejszy `lastFinish`).
**Dlaczego:** „najdłużej bezczynny" = najdawniej skończył = **najmniejszy** `lastFinish`.
Przy `-lastFinish` (min = najlepszy) wybierany był zasób/kanał, który skończył **najświeżej**,
czyli dokładnie odwrotność definicji z §4.1/§4.2.

## 3. Fallback „same as predecessor" spójny z celem (koszt)
**Plik:** `src/evaluator.cpp`
**Co:** gdy procesor rodzica jest niedostępny, fallback = `Cheapest` (było `Fastest`) —
w `pickSingleProc` i `pickCommonProcs`.
**Dlaczego:** optymalizujemy koszt (§1). `Fastest` podnosił koszt wbrew celowi;
`Cheapest` jest rekomendacją z §7/§14.

## 4. Test nowego solvera (nowy plik)
**Plik:** `tests/test_evaluator.cpp` (+ wpis w `CMakeLists.txt`)
**Co:** instancje policzalne ręcznie, sprawdzają:
- koszt/makespan łańcucha (18 / 8) i niezmienniczość względem genu przy 1 procesorze,
- komunikację między procesorami: transfer `ceil(10/7)=2`, `connectCost 15+15`, koszt 48,
- transfer lokalny (ten sam procesor) = darmowy i natychmiastowy mimo `data=10`,
- karę `Tmax` (`fitness = cost + λ·(makespan − Tmax)`) oraz brak kary przy luźnym `Tmax`,
- determinizm evaluatora i determinizm `runGa` dla stałego `seed`.
**Dlaczego:** dotąd testy pokrywały tylko parser i przestarzały `gp_tree`;
`evaluator`/`ga`/`genes`/`spanning_tree` nie miały żadnego testu.

## Świadomie NIE zmienione
- **Cichy „darmowy transfer" przy braku legalnego kanału** (`applyTransfer` → 0):
  zgodne z założeniem §2.5 („dla poprawnych instancji legalny kanał zawsze istnieje,
  brak kary za brak kanału"). Do rewizji dopiero, jeśli zmienimy to założenie.
- **Semantyka genów LFU/idle/same-pred dla podzbiorów CDT/CGT** poza samym kierunkiem idle —
  pozostaje pozycją otwartą (§14).

## Jak sprawdzić
```bash
cmake -S . -B build-mac
cmake --build build-mac
ctest --test-dir build-mac --output-on-failure
```
