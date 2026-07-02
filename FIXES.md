# Poprawki solvera GA — dla review

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
