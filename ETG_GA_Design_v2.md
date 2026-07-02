# Projekt ETG — opis problemu i projekt algorytmu genetycznego (v2)

> Dokument roboczy dla zespołu. Opisuje **problem**, **model ETG**, **uzgodnione podejście ewolucyjne**
> (wg wykładu) oraz szczegóły implementacyjne wystarczające, by Cursor/agent mógł kodować
> bez domyślania kluczowej logiki.
>
> **v2 vs v1 (`ETG_GA_Design.md`):**
> - status: **solver bazowy już istnieje** (`src/genes`, `src/spanning_tree`, `src/evaluator`, `src/ga`) —
>   sekcje 1/5/11 nie mówią już „do implementacji", tylko opisują stan i to, co zostało do domknięcia,
> - **wagi prawdopodobieństw genów (§4.3) są zdecydowane i poprawne** — mają zostać **wpięte w kod**
>   (obecnie `genes.cpp` losuje uniform); to zadanie implementacyjne, nie punkt otwarty,
> - **jawna decyzja projektowa**: drzewo rozpinające jest **stałe i deterministyczne**, ewoluują tylko
>   geny w węzłach (§3.3),
> - **nowa §13 — wariant rozszerzony (extended)**: pełny opis rozszerzonego schematu ewolucyjnego
>   (**α/β/γ/δ**, **rank selection**, **dynamiczny warunek stopu**) wraz z instrukcją,
>   **jak rozwinąć obecny kod**, żeby dało się to łatwo dołożyć bez ruszania evaluatora,
> - pozycje **otwarte (§14)** pozostają otwarte, ale mają teraz dokładny opis.
>
> **Status:** specyfikacja bazowa zamknięta i zaimplementowana; warstwa ewolucyjna ma dwa warianty —
> **bazowy** (§9) i **rozszerzony — extended** (§13); oba zaimplementowane, przełączane `--scheme`.
> **Nie dotyczy** starszego planu z `CGP_Algorithm_Plan.md` (reguła priorytetu + drzewo wyrażeń GP).

---

## 1. Cel projektu

**Wejście:**

- plik ETG (Extended Task Graph) opisujący proces (np. produkcja samolotu),
- parametr `Tmax` — twardy limit czasu (makespan), przekazywany jako argument programu (`--tmax N`).

**Wyjście:**

- harmonogram: przypisanie zadań do procesorów (zasobów), wybór kanałów komunikacyjnych,
  czasy startu/końca, łączny koszt.

**Optymalizacja:**

| | Opis |
|---|---|
| **Cel (fitness)** | minimalizacja **kosztu** całego harmonogramu |
| **Ograniczenie** | makespan ≤ `Tmax` (warunek twardy) |
| **Metoda** | algorytm genetyczny z genotypem w postaci **drzewa rozpinającego graf zadań** |

Parser i walidacja ETG są gotowe (`src/etg.cpp`, `src/etg_prep.cpp`).
**Solver GA jest zaimplementowany w całości** (`src/genes.*`, `src/spanning_tree.*`,
`src/evaluator.*`, `src/ga.*`, `src/main.cpp`): wagi genów §4.3 wpięte, subtree crossover §9.3,
oba warianty warstwy ewolucyjnej — bazowy (§9) i rozszerzony (§13, `runGaExtended`) — przełączane
flagą `--scheme basic|extended`. Otwarte pozostają tylko punkty §14.

---

## 2. Model ETG (skrót)

Pełna specyfikacja: `Input_format.md`, `ETG_concept.md`.

### 2.1 Graf zadań (DAG)

- Węzły = zadania `T0 … T(n-1)`.
- Krawędź `u → v` = zależność; opcjonalnie **`data`** = wolumen danych do przesłania (0 = sama precedencja).

### 2.2 Kategorie zadań

| Prefix | Kategoria | Przypisanie |
|---|---|---|
| `GT` | General | 1 procesor dowolnego typu |
| `DT` | Dedicated | 1 procesor **wyspecjalizowany** (sentinels mogą zawęzić wybór) |
| `UT` | Universal | 1 procesor **uniwersalny** |
| `CDT` | Common dedicated | **≥1** procesorów wyspecjalizowanych **jednocześnie** |
| `CGT` | Common general | **≥1** procesorów dowolnego typu **jednocześnie** |

Dozwolone procesory dla zadania `t`: `PreparedData::allowed[t]` (kategoria + sentinels `< 0` w macierzach).

### 2.3 Procesory (`@proc`)

- **Uniwersalny** (`typeFlag = 1`): wiele zadań **sekwencyjnie** (jedno naraz).
- **Wyspecjalizowany** (`typeFlag = 0`): **dokładnie jedno** zadanie w całym harmonogramie.
- **Koszt zakupu** `buyCost`: płacony raz, gdy procesor wykona ≥1 zadanie.

### 2.4 Zadania common (CDT / CGT)

Dla zbioru procesorów `S`, `k = |S|`:

```
czas_zadania  = Σ (times[t][p] / k)  dla p ∈ S
koszt_zadania = Σ (costs[t][p] / k)  dla p ∈ S
```

Komunikacja **z** common task: każdy z `k` procesorów wysyła **`data/k`** po każdej krawędzi wychodzącej z `data > 0`.

### 2.5 Komunikacja (`@comm`)

- Kanały to **infrastruktura poza grafem zadań** (osobna sekcja pliku).
- Transfer `u → v` z `data > 0` wymaga kanału tylko gdy `proc(u) ≠ proc(v)`.
- Ten sam procesor → transfer **lokalny** (koszt 0, czas 0).
- Kanał legalny dla pary `(ps, pr)`: `canConnect[ps] == 1 && canConnect[pr] == 1`.
- `connectCost` — płacony **raz** na parę (kanał, procesor), która faktycznie przesyła dane.
- Czas transmisji `d` jednostek: `ceil(d / bandwidth)`.

**Założenie projektowe:** dla poprawnych instancji **legalny kanał zawsze istnieje** (brak kary za brak kanału).

---

## 3. Podejście ewolucyjne — ogólna idea

### 3.1 Co ewoluujemy

Nie optymalizujemy bezpośrednio harmonogramu ani wzoru priorytetu (`+`, `×`, `cost`…).
Ewoluujemy **zestaw heurystyk** przypisania procesora i wyboru kanału — po jednej parze genów
**na każde zadanie (węzeł)**.

### 3.2 Genotyp vs fenotyp

```
GENOTYP (stały kształt)
  drzewo rozpinające DAG-u  —  tylko struktura (crossover, „same as predecessor")
  per task t:
    PeGene  — jak wybrać procesor(y) dla t
    ClsGene — jak wybrać kanał, gdy t odbiera dane z innego procesora

         │  evaluator (kolejność topologiczna ETG)
         ▼

FENOTYP
  konkretny harmonogram + koszt + makespan

         │
         ▼

FITNESS
  koszt (+ kara za Tmax, + kara za niewykonalne przypisanie)
```

### 3.3 Dlaczego drzewo rozpinające

- ETG to DAG (może mieć wiele poprzedników).
- **Genotyp** ma kształt **drzewa** rozpinającego ten DAG (n węzłów, n−1 krawędzi drzewa).
- Kształt jest **identyczny u wszystkich osobników** → crossover nie psuje topologii genotypu.
- Drzewo służy m.in. genowi **„Same as predecessor"** (procesor rodzica **w drzewie**, nie dowolnego poprzednika w DAG).

**Budowa (deterministyczna, raz na instancję):**

```
dla każdego t w porządku topologicznym:
  parent[t] = poprzednik z preds[t] o najmniejszym id
  (brak poprzedników → parent[t] = -1, korzeń)
```

Komunikacja **nie jest** węzłem drzewa genotypu — powstaje w evaluatorze z przypisań + krawędzi ETG.

> **Świadoma decyzja projektowa (interpretacja wykładu).**
> **Kształt drzewa jest stały i identyczny u wszystkich osobników** — nie ewoluuje; ewoluują
> **wyłącznie tablice genów** `peGenes[t]`/`clsGenes[t]`. Crossover działa **na poddrzewach tego
> stałego drzewa** (subtree crossover, §9.3): losujemy węzeł i wymieniamy geny **całego poddrzewa**
> między rodzicami — dokładnie „skopiuj tę samą część drzewa" ze slajdu. Ponieważ wszyscy dzielą
> ten sam kształt, wymiana poddrzewa nigdy nie psuje topologii, a gen „same as predecessor" ma
> zawsze dobrze zdefiniowanego rodzica. Nie ewoluujemy samej *struktury* drzewa (różnych kształtów
> per osobnik) — nie jest to potrzebne, bo całą zmienność decyzji niosą geny w węzłach.

---

## 4. Zbiór genów (function set)

Gen = **wybór heurystyki** z poniższych list (enum), **nie** węzeł drzewa wyrażenia.

### 4.1 Genes PEs — przypisanie procesora (węzeł / task)

| Id | Nazwa (slajd) | Semantyka w ETG |
|---|---|---|
| `AllocCheapest` | Allocated → cheapest | Min koszt w `usedProcs ∩ allowed[t]` ∩ dostępnych |
| `AllocFastest` | Allocated → fastest | Min `times[t][p]` w allocated |
| `AllocLFU` | Allocated → LFU | Procesor z **najmniejszą liczbą użyć** w harmonogramie |
| `AllocIdle` | Allocated → idle longest | Procesor **najdłużej bezczynny** = **najmniejszy** `lastFinish[p]` |
| `AllocSamePred` | Allocated → same as predecessor | Procesor **rodzica w drzewie rozpinającym** |
| `Cheapest` | Smallest area / cheapest (global) | Min koszt w `allowed[t]` ∩ dostępnych |
| `Fastest` | Fastest (global) | Min czas w legalnych i dostępnych |
| `MinTS` | Min(t×s) | Min `times[t][p] × costs[t][p]` |

**Mapowanie „cheapest" (PE):**

- koszt wykonania: `costs[t][p]`,
- przy **pierwszym** użyciu procesora `p` dolicz `procs[p].cost()` (buyCost).

**„Allocated" (PE):** procesory **już użyte** w bieżącym harmonogramie (`usedProcs`),
po filtracji przez `allowed[t]` i dostępność (patrz §6).

> **Uwaga implementacyjna — kierunek „idle longest".**
> „Najdłużej bezczynny" = ten, który **najdawniej** skończył ostatnie zadanie = **najmniejszy**
> `lastFinish[p]`. W scoringu (min = lepszy) należy użyć `score = lastFinish[p]` (a **nie** `-lastFinish[p]`).
> Analogicznie dla kanałów `ClsAllocIdle`: `score = channelLastFinish[c]`.
> (Definicja słowna jest tu źródłem prawdy; obecna implementacja ma to odwrócone — do poprawy.)

### 4.2 Genes CLS — wybór kanału (węzeł / task — odbiorca)

Stosowany przy każdej krawędzi ETG `u → t` z `data > 0`, gdy `proc(u) ≠ proc(t)`.
Reguła brana z **`ClsGene[t]`** (węzeł odbiorcy).

| Id | Nazwa (slajd) | Semantyka w ETG |
|---|---|---|
| `ClsAllocCheapest` | Allocated → cheapest | Min `connectCost` w kanałach już podłączonych |
| `ClsAllocFastest` | Allocated → fastest | Max `bandwidth` w allocated |
| `ClsAllocLFU` | Allocated → LFU | Kanał z najmniejszą **liczbą użyć** |
| `ClsAllocIdle` | Allocated → idle longest | Kanał **najdłużej bezczynny** = **najmniejszy** `channelLastFinish[c]` |
| `ClsCheapest` | Smallest area / cheapest | Min `connectCost` spośród legalnych |
| `ClsHighestBw` | Highest bandwidth | Max `bandwidth` spośród legalnych |
| `ClsLFU` | Least frequently used | Globalnie najrzadziej używany legalny kanał |

**„Allocated" (CLS):** kanały, do których **już podłączono procesor** w tym harmonogramie
(`connectCost` za tę parę (kanał, procesor) został już zapłacony / kanał „aktywny").

**Legalny kanał** dla `(ps, pr)`: oba procesory mają `canConnect == 1`.

### 4.3 Prawdopodobieństwa genów (ZDECYDOWANE — do wpięcia w kod)

Wagi są **rozstrzygnięte i poprawne**. To **rozkład dwupoziomowy**: najpierw losujemy grupę na poziomie
górnym, a jeśli wypadła grupa „Allocated", losujemy wewnątrz niej (wagi wewnętrzne sumują się do 100%
**tych 60%**). Efektywne prawdopodobieństwo genu z grupy Allocated = `0.60 × waga_wewnętrzna`.

**PE — poziom górny (Σ = 100%):**

| Grupa / gen | Waga górna |
|---|---|
| **PEAllocated** | **60%** |
| PECheapest (`Cheapest`) | 10% |
| PEFastest (`Fastest`) | 10% |
| PEMin(t×c) (`MinTS`) | 20% |

**PE — wewnątrz PEAllocated (Σ = 100% z 60%):**

| Gen | Waga wewn. | Efektywnie |
|---|---|---|
| `AllocCheapest` | 20% | 12% |
| `AllocFastest` | 20% | 12% |
| `AllocLFU` | 20% | 12% |
| `AllocIdle` | 20% | 12% |
| `AllocSamePred` | 20% | 12% |

**CLS — poziom górny (Σ = 100%):**

| Grupa / gen | Waga górna |
|---|---|
| **ClsAllocated** | **60%** |
| ClsCheapest | 10% |
| ClsHighestBw | 10% |
| ClsLFU | 20% |

**CLS — wewnątrz ClsAllocated (Σ = 100% z 60%):**

| Gen | Waga wewn. | Efektywnie |
|---|---|---|
| `ClsAllocCheapest` | 20% | 12% |
| `ClsAllocFastest` | 20% | 12% |
| `ClsAllocLFU` | 30% | 18% |
| `ClsAllocIdle` | 30% | 18% |

**Implementacja (do zrobienia):** `randomPeGene` / `randomClsGene` w `src/genes.cpp` mają losować
**wg powyższych wag** (obecnie losują uniform). Najprościej: `std::discrete_distribution<int>`
z tablicą wag efektywnych (8 wartości dla PE, 7 dla CLS) — jeden rzut zamiast dwupoziomowego,
matematycznie równoważny. Wagi trzymać jako `constexpr` obok enumów, żeby były jednym źródłem prawdy.

---

## 5. Reprezentacja osobnika (kod)

```cpp
enum class PeGene { AllocCheapest, AllocFastest, AllocLFU, AllocIdle, AllocSamePred,
                    Cheapest, Fastest, MinTS, COUNT };

enum class ClsGene { ClsAllocCheapest, ClsAllocFastest, ClsAllocLFU, ClsAllocIdle,
                     ClsCheapest, ClsHighestBw, ClsLFU, COUNT };

struct SpanningTree {
    std::vector<int> parent;   // parent[taskId], -1 = korzeń
};

struct Individual {
    std::vector<PeGene>  peGenes;   // rozmiar numTasks, indeks = taskId
    std::vector<ClsGene> clsGenes;  // rozmiar numTasks
    Schedule schedule;              // fenotyp
    double fitness = 1e18;          // mniejsze = lepiej
};
```

Ta reprezentacja jest już w `src/genes.h` / `src/schedule.h` / `src/spanning_tree.h`.
Istniejący `gp_tree` (reguła priorytetu) **nie jest** częścią tego podejścia — może zostać w repo (testy).

---

## 6. Evaluator — budowa harmonogramu

> Zaimplementowany w `src/evaluator.cpp` (`evaluateIndividual`). Poniżej semantyka referencyjna.

### 6.1 Kolejność

Zadania planowane w **`PreparedData::topo`** (porządek topologiczny **całego DAG**, nie DFS drzewa genotypu).

### 6.2 Stan evaluatora (śledzony w trakcie)

| Pole | Znaczenie |
|---|---|
| `freeAt[p]` | kiedy procesor `p` będzie wolny (uniwersalne) |
| `lastFinish[p]` | czas zakończenia ostatniego zadania na `p` (idle PE — mniejszy = dłużej bezczynny) |
| `peUseCount[p]` | LFU procesorów |
| `usedProcs` | zbiór procesorów, które wykonały ≥1 zadanie |
| `burnedSpecialized` | wyspecjalizowane już zajęte |
| `channelConnected[c][p]` | czy procesor `p` podłączony do kanału `c` |
| `channelUseCount[c]` | LFU kanałów |
| `channelLastFinish[c]` | idle kanałów (mniejszy = dłużej bezczynny) |
| `finish[t]`, `assignments[t]` | wynik |

### 6.3 Wybór procesora — GT / DT / UT

1. Zbuduj kandydatów: `allowed[t]` ∩ procesory **dostępne** (wyspecjalizowany nie w `burnedSpecialized`).
2. Zastosuj regułę `PeGene[t]` (z fallbackami — §7).
3. Jeden procesor `p`.

### 6.4 Wybór procesora — CDT / CGT

1. Niech `C ⊆ allowed[t]` to procesory dostępne.
2. Dla każdego **niepustego** podzbioru `S ⊆ C` (spełniającego ograniczenia dostępności):
   - policz `time(S)`, `cost(S)` wg modelu `1/k`,
   - oceń wg **kryterium aktywnego genu** (`PeGene[t]`):
     - `Cheapest` / `AllocCheapest` → min koszt,
     - `Fastest` / `AllocFastest` → min czas,
     - `MinTS` → min iloczyn/suma wg uzgodnionej formuły na cały common task,
     - LFU / idle / same-as-pred — zastosuj regułę do wyboru **reprezentanta** lub pierwszego elementu, potem ewaluuj podzbiory zawierające wynik reguły *(implementacja: brute force podzbiorów + scoring wg genu; patrz §14)*.
3. Wybierz **najlepszy podzbiór** `S` według kryterium genu.
4. Przypisz wszystkie procesory z `S`.

**Uwaga złożoności:** do `2^|allowed[t]|` podzbiorów — OK dla typowych instancji projektu (limit w §14).

### 6.5 Czas startu i zakończenia zadania

```
dataReady(t) = max po poprzednikach u: finish[u] + commDelay(u→t)
start(t)     = max( dataReady(t), max_{p ∈ S} freeAt[p] )   // S = {p} lub zbiór dla common
execTime(t)  = wg modelu (pojedynczy / common 1/k)
finish(t)    = start(t) + execTime(t)
```

**Ten sam procesor co poprzednik / sekwencja na uniwersalnym:**

czas zakończenia = moment gotowości + **oczekiwanie na wolny procesor** + czas wykonania
(tj. `start = max(..., freeAt[p])`, nie ignorujemy kolejki na uniwersalnym).

### 6.6 Komunikacja

Dla każdego poprzednika `u` zadania `t`:

- jeśli `data == 0` → tylko precedencja (`finish[u]`),
- jeśli `proc(u) == proc(t)` (lub wspólny procesor w common) → lokalnie, 0,
- inaczej:
  - wybierz kanał `c = applyCls(clsGene[t], ps, pr, stan)`,
  - `commDelay += ceil(dataPiece / c.bandwidth)` (dla common: `dataPiece = data/k`),
  - dolicz `connectCost` za nowe pary (kanał, procesor) nadawcy/odbiorcy,
  - zaktualizuj liczniki LFU/idle kanału.

### 6.7 Koszt całkowity

```
koszt = Σ koszt wykonania zadań
      + Σ buyCost[p] dla p ∈ usedProcs
      + Σ koszty podłączeń kanałów (connectCost per para kanał–procesor)
```

```
makespan = max_t finish[t]
```

---

## 7. Fallbacki i kary

| Sytuacja | Zachowanie |
|---|---|
| **Allocated (PE) puste** | fallback na global **`Cheapest`** (optymalizujemy koszt) |
| **Same as predecessor** niedostępny / niellegalny | fallback z uwzględnieniem **czasu** (np. najbliższy dostępny minimalizujący opóźnienie — lub Cheapest spośród dostępnych; patrz §14) |
| **Brak dostępnego procesora** | **kara** fitness (bardzo duża, np. `1e12`) |
| **Wszystkie legalne procesory zajęte** | **kara** |
| **Brak legalnego kanału** | nie zakładamy (instancja poprawna) |

---

## 8. Fitness

```
jeśli makespan ≤ Tmax:   fitness = totalCost
jeśli makespan > Tmax:   fitness = totalCost + λ · (makespan − Tmax)
```

- `λ` — duża stała lub parametr CLI (np. `1000 × typowy_koszt`).
- Rozwiązanie dopuszczalne w czasie **zawsze lepsze** niż przekraczające Tmax przy sensownym λ.
- **Determinizm:** ten sam genotyp → ten sam harmonogram i fitness.

---

## 9. Operatory genetyczne — wariant BAZOWY (zaimplementowany)

> To jest wariant w `src/ga.cpp` (`runGa`). Schemat **rozszerzony (extended)** — α/β/γ/δ, rank
> selection, dynamiczny stop — jest opisany w **§13** jako rozszerzenie tej warstwy — evaluator (§6)
> i geny (§4) pozostają bez zmian.

### 9.1 Inicjalizacja

- `POP` osobników (parametr `--pop`, domyślnie 50).
- Dla każdego `t`: losowy `PeGene[t]` i `ClsGene[t]` **wg wag §4.3** (zaimplementowane w `genes.cpp`).

### 9.2 Selekcja

- Turniejowa (k=3): wygrywa niższy fitness. *(W §13 zastąpiona rank selection.)*

### 9.3 Crossover

- **Subtree crossover (główny operator):** losujemy węzeł `r` drzewa rozpinającego; dziecko = kopia
  rodzica A, po czym geny `(PeGene, ClsGene)` **wszystkich węzłów poddrzewa zakorzenionego w `r`**
  (czyli `r` i jego potomkowie w drzewie) nadpisujemy wartościami z rodzica B. Kształt drzewa jest
  identyczny u obu rodziców, więc zbiór węzłów poddrzewa jest ten sam → operacja jest bezpieczna i nie
  psuje struktury. Poddrzewo wyznaczamy z `SpanningTree::parent` (zejście po potomkach `r`).
  Jedna operacja = **2 rodziców → 1 dziecko**.
- **Wariant pomocniczy (uniform per węzeł):** dla każdego `t` niezależnie weź `(PeGene, ClsGene)` od A
  lub B. Prostszy, ale nie zachowuje spójnych „bloków" decyzji wzdłuż gałęzi — używać tylko pomocniczo.

> **Status:** zrealizowane — `ga.cpp` implementuje **subtree crossover** jako główny operator
> (funkcja `subtreeCrossover`, używana w `runGa` i `runGaExtended`).

### 9.4 Mutacja

- Losowy task `t` → nowy `PeGene[t]`; losowy task `t` → nowy `ClsGene[t]`.
- Prawdopodobieństwo mutacji `Pm` (parametr).

### 9.5 Elitizm

- Kopiuj `E` najlepszych bez zmian (domyślnie 2). *(W §13 uogólnione do klonowania `δ`.)*

### 9.6 Parametry (CLI)

`--pop`, `--generations`, `--seed`, `--lambda`, `--tmax` (patrz `main.cpp`).

---

## 10. Przepływ programu

```
parseETG(path)
validateETG(graph)
prep = prepare(graph)           // allowed, preds, topo, nSucc
tree = buildSpanningTree(prep)  // deterministyczne

pop = initPopulation(POP, prep, tree)

for gen in 1..GENERATIONS:                 // wariant bazowy §9
    for each individual:
        evaluate(ind, graph, prep, tree, Tmax)  // fenotyp + fitness
    pop = select + crossover + mutate + elitism(pop)

output best individual's schedule
```

Wariant rozszerzony (§13) zamienia pętlę `for gen` na pętlę z **dynamicznym stopem** i konstrukcją
pokolenia wg frakcji **β/γ/δ**.

---

## 11. Pliki w repozytorium

| Plik | Rola | Status |
|---|---|---|
| `src/etg.h`, `src/etg.cpp` | model ETG, parser, walidacja | gotowe |
| `src/etg_prep.h`, `src/etg_prep.cpp` | `allowed`, `preds`, `topo` | gotowe |
| `src/genes.h`, `src/genes.cpp` | enumy `PeGene`/`ClsGene`, losowanie wg wag §4.3 | gotowe |
| `src/spanning_tree.h`, `src/spanning_tree.cpp` | budowa drzewa rozpinającego | gotowe |
| `src/schedule.h` | `Schedule`, `Individual` | gotowe |
| `src/evaluator.h`, `src/evaluator.cpp` | PE/CLS + harmonogram + fitness | gotowe |
| `src/ga.h`, `src/ga.cpp` | subtree crossover, wariant bazowy §9 + rozszerzony §13 (`runGaExtended`) | gotowe |
| `src/main.cpp` | CLI obu schematów (`--scheme basic\|extended`), uruchomienie | gotowe |
| `src/gp_tree.*`, `src/gp_ops.*` | stary plan (reguła priorytetu) — **poza tym designem** | archiwum |
| `CGP_Algorithm_Plan.md` | stary plan zespołu — **nieaktualny** | archiwum |
| `ETG_GA_Design.md` | specyfikacja v1 | zastąpiona przez v2 |
| **`ETG_GA_Design_v2.md`** | **ten dokument — aktualna specyfikacja** | — |

---

## 12. Mapa pojęć (wykład ↔ projekt)

| Wykład | Projekt ETG |
|---|---|
| PE (Processing Element) | Procesor / zasób (`Processor`) |
| CLS (Communication Link) | Kanał (`CommChannel`) |
| Area | Koszt (wykonania + ewentualnie buyCost) |
| Task graph | ETG DAG |
| Genotyp = drzewo | Drzewo rozpinające + geny PE/CLS per task |
| Fenotyp | Harmonogram (`Schedule`) |
| Rank selection | §13 (wariant extended) |
| α · n · (liczba typów PE) | rozmiar populacji (§13) |
| β / γ / δ | frakcje: crossover / mutacja / klonowanie (§13) |

---

## 13. Wariant rozszerzony (extended): schemat α/β/γ/δ, rank selection, dynamiczny stop

> **Cel tej sekcji:** rozszerzyć bazowy pomysł (§9) o pełny schemat ewolucyjny α/β/γ/δ,
> **nie ruszając** evaluatora (§6), genów (§4) ani reprezentacji (§5). To „nakładka" na `ga.cpp` —
> dlatego wariant bazowy i rozszerzony współistnieją i przełącza się je flagą CLI.
> Poniżej: definicje, wzory, konkretny plan zmian w kodzie i pseudokod pętli.
>
> **Status: ZAIMPLEMENTOWANE** — `runGaExtended` w `src/ga.cpp` (+ `countPeTypes`,
> `linearRankProbs`, `validateExtendedParams`, `mutateForce`), CLI w `main.cpp`
> (`--scheme extended --alpha --beta --gamma --delta --rank-pressure --no-improve --max-gen`),
> testy w `tests/test_ga.cpp`.

### 13.1 Definicje

- **Rozmiar populacji (stała na całe uruchomienie):**
  ```
  POP = round( α · n · τ )
  ```
  gdzie `n = numTasks`, `α` — dowolne dodatnie, a `τ` = **liczba typów PE (procesorów) w systemie**.
  **Ustalona definicja `τ` (decyzja projektowa):** `τ` = **liczba różnych wartości `typeFlag`
  występujących w `@proc`** — czyli 1 lub 2 (uniwersalne / wyspecjalizowane). Zawsze `≥ 1`,
  niezależna od skali kosztów, prosta do policzenia. Implementacyjnie: `countPeTypes(graph)`
  zwraca liczbę różnych `typeFlag` obecnych w instancji (patrz §13.3). Funkcję trzymamy osobno,
  więc gdyby definicję trzeba było kiedyś zmienić (np. liczyć rozróżnialne modele zasobów),
  podmienia się tylko jej ciało — reszta algorytmu bez zmian.

- **Frakcje pokolenia (podział rozłączny, `β + γ + δ = 1`):**
  ```
  nCross = round( β · POP )   // osobniki tworzone przez crossover
  nMut   = round( γ · POP )   // osobniki tworzone przez mutację,   γ ∈ (0,1)
  nClone = POP − nCross − nMut  // ≈ round(δ · POP), klonowanie,     δ ∈ (0,1)
  ```
  `α` — dowolne; `β, γ, δ ∈ (0,1)`; suma `β+γ+δ = 1` (walidacja przy starcie).
  `nClone` liczymy jako resztę, żeby suma zawsze dała dokładnie `POP` (unikamy błędów zaokrągleń).

- **Rank selection (selekcja rangowa liniowa):** sortujemy populację po fitness rosnąco
  (rang `i = 0` = najlepszy). Prawdopodobieństwo wyboru osobnika o randze `i`:
  ```
  p(i) = ( 1 / POP ) · ( sp − (2·sp − 2) · i / (POP − 1) )
  ```
  gdzie `sp ∈ [1.0, 2.0]` to nacisk selekcyjny (`selection pressure`, domyślnie `1.5`).
  Dla `sp = 1` → selekcja uniform; dla `sp = 2` → najsilniejsza preferencja lepszych.
  Rank selection używamy do wyboru rodziców crossover, osobnika do mutacji **i** osobnika do klonowania
  (na wykładzie „Mutation: wybiera pewną liczbę osobników dzięki selection").

- **Dynamiczny warunek stopu:** kończymy, gdy **najlepszy fitness nie poprawił się** przez
  `noImproveLimit` kolejnych pokoleń (parametr `K`). Dodatkowo twardy limit `maxGenerations`
  jako bezpiecznik. „Poprawa" = spadek best fitness o co najmniej `eps` (np. `1e-9`).

### 13.2 Odwzorowanie: co zamienia co (bazowy → rozszerzony)

| Wariant bazowy §9 (`runGa`) | Wariant rozszerzony §13 (`runGaExtended`) |
|---|---|
| `populationSize` stałe (CLI) | `POP = round(α·n·τ)` liczone z instancji |
| tournament (k=3) | rank selection (liniowa, `sp`) |
| `crossoverRate` (rzut monetą per para) | `nCross = round(β·POP)` — twarda liczba |
| `mutationRate` (rzut per gen) | `nMut = round(γ·POP)` — twarda liczba |
| `eliteCount` (kopiuj E najlepszych) | `nClone = round(δ·POP)` — klonowanie rank-selection (elityzm = klon najlepszego) |
| `generations` stałe | dynamiczny stop `noImproveLimit` + `maxGenerations` |

Operatory **wewnętrzne** (`crossover` = subtree crossover §9.3, `mutate` = wybór węzła + podmiana opcji)
oraz `evaluateIndividual` **zostają bez zmian** — zmienia się tylko **sposób składania nowego pokolenia**.

### 13.3 Plan zmian w kodzie (minimalny, bez ruszania evaluatora)

1. **`GaParams` (w `ga.h`)** — dodać pola rozszerzone, zachować bazowe (przełącznik `useExtendedScheme`):
   ```cpp
   struct GaParams {
       bool   useExtendedScheme = false;  // false = §9 (bazowy), true = §13

       // --- schemat rozszerzony (§13) ---
       double alpha = 1.0;      // POP = round(alpha * numTasks * numPeTypes)
       double beta  = 0.6;      // frakcja crossover
       double gamma = 0.3;      // frakcja mutacji,     (0,1)
       double delta = 0.1;      // frakcja klonowania,  (0,1);  beta+gamma+delta = 1
       double rankPressure = 1.5;   // sp ∈ [1,2] dla rank selection
       int    noImproveLimit = 20;  // dynamiczny stop
       int    maxGenerations = 1000;// bezpiecznik

       // --- wariant bazowy §9 (zostaje) ---
       int populationSize = 50;
       int generations = 100;
       int eliteCount = 2;
       int tournamentSize = 3;
       double crossoverRate = 0.7;
       double mutationRate = 0.1;

       unsigned seed = 42;
   };
   ```

2. **Liczba typów PE** — mała funkcja pomocnicza (np. w `ga.cpp` lub `etg_prep`):
   ```cpp
   int countPeTypes(const etg::ETG& g);  // liczba różnych typeFlag obecnych w @proc (1 lub 2)
   ```

3. **Rank selection** — funkcja zwracająca indeks osobnika z **posortowanej** populacji
   wg rozkładu `p(i)` z §13.1 (użyć `std::discrete_distribution` na wektorze `p(0..POP-1)`).

4. **Walidacja parametrów** przy starcie: `abs(beta+gamma+delta - 1) < 1e-9`,
   `gamma∈(0,1)`, `delta∈(0,1)`, `alpha>0`, `rankPressure∈[1,2]`. Błąd → komunikat i exit 1.

5. **Nowa pętla ewolucji** (`runGaExtended`, wybierana gdy `useExtendedScheme`):
   ```
   POP = max(2, round(alpha * numTasks * countPeTypes(graph)))
   pop = initPopulation(POP)                 // geny wg wag §4.3
   evaluateAll(pop)
   best = argmin fitness;  bestFit = best.fitness;  noImprove = 0
   for gen in 1..maxGenerations:
       ranked = sort(pop) by fitness asc
       probs  = linearRankProbs(POP, rankPressure)

       nCross = round(beta  * POP)
       nMut   = round(gamma * POP)
       nClone = POP - nCross - nMut          // reszta = delta

       next = []
       // klonowanie (elityzm: 1. klon = najlepszy, reszta rank-selection)
       next.push( ranked[0] )                // gwarancja niepogorszenia
       repeat (nClone - 1): next.push( ranked[ rankSelect(probs) ] )
       // crossover  (2 rodziców -> 1 dziecko)
       repeat nCross:
           a = ranked[rankSelect(probs)];  b = ranked[rankSelect(probs)]
           next.push( subtreeCrossover(a, b) )   // §9.3: wymiana poddrzewa
       // mutacja  (osobny kanał: 1 wybrany -> 1 mutant, NIE dzieci z crossoveru)
       repeat nMut:
           m = clone( ranked[rankSelect(probs)] )
           mutateForce(m)                    // wybór węzła + podmiana genu (zawsze, nie per Pm)
           next.push( m )

       pop = next
       evaluateAll(pop)
       cur = argmin fitness
       if cur.fitness < bestFit - eps: best = cur; bestFit = cur.fitness; noImprove = 0
       else: noImprove++
       if noImprove >= noImproveLimit: break

   return best
   ```
   **Doprecyzowanie operatorów (żeby nie było wątpliwości):**
   - `linearRankProbs(POP, sp)` — zwraca wektor `p(0..POP-1)` ze wzoru §13.1 (najlepszy = indeks 0).
   - `rankSelect(probs)` — losuje **jeden** indeks rangi wg `std::discrete_distribution(probs)`;
     używany identycznie dla crossoveru, mutacji i klonowania.
   - **Trzy kanały są rozłączne** i każdy wypełnia swoją część `POP`:
     - crossover: `rankSelect` **dwóch** rodziców → `subtreeCrossover` → **1 dziecko** (§9.3),
     - mutacja: `rankSelect` **jednego** osobnika → klon → `mutateForce` → **1 mutant**
       (to **osobne źródło**, nie „doszlifowanie" dzieci z crossoveru),
     - klonowanie: `rankSelect` jednego → kopia bez zmian.
   - `mutateForce` = mutacja, która **na pewno** podmienia gen w wylosowanym węźle (liczbę mutantów
     wyznacza `γ`, nie prawdopodobieństwo `Pm`). Realizacja: parametr do istniejącego `mutate` lub osobna funkcja.
   - `subtreeCrossover` = operator z §9.3 (wymiana genów całego poddrzewa; potomków `r` liczymy z `parent[]`).

6. **`main.cpp` / CLI** — dodać flagi:
   `--scheme extended|basic`, `--alpha`, `--beta`, `--gamma`, `--delta`,
   `--rank-pressure`, `--no-improve`, `--max-gen`. Router wybiera `runGa` albo `runGaExtended`.

### 13.4 Uwagi i pułapki

- **Determinizm**: całość zależy tylko od `seed` — ten sam `seed` + te same parametry → ten sam wynik.
- **`nClone ≥ 1`**: trzymamy przynajmniej klon najlepszego (elityzm), by best nie mógł zniknąć.
  Jeśli `round(δ·POP) = 0`, wymuszamy `nClone = 1` i odejmujemy 1 od największej z pozostałych frakcji.
- **Małe `POP`**: dla malutkich instancji `α·n·τ` może dać < 2 — klampujemy do min. 2.
- **Rank vs fitness z karą**: rank selection patrzy tylko na *porządek* fitness, więc kara za Tmax
  (§8) działa poprawnie także tutaj (osobniki łamiące Tmax lądują na gorszych rangach).
- **Kompletność schematu**: `β+γ+δ=1`, `α` dowolne, `γ,δ∈(0,1)`, dynamiczny stop, rank selection —
  wszystkie punkty schematu „Alpha, Beta, Gamma" są tu odwzorowane.

---

## 14. Rozstrzygnięcia i punkty otwarte

Najpierw **decyzje** (były otwarte, teraz zamknięte — cytaty poglądowe), potem realnie **otwarte** punkty.

> **Rozstrzygnięte — definicja `τ`:** do wzoru `POP = α·n·τ` przyjęto
> **liczbę różnych `typeFlag` w `@proc`** (1 lub 2). Szczegóły w §13.1.

> **Rozstrzygnięte — fallback `AllocSamePred`:** gdy procesor rodzica jest niedostępny/niellegalny,
> stosujemy **(b) `Cheapest` spośród dostępnych** (spójne z celem = koszt). Już zaimplementowane
> w `evaluator.cpp` (`pickSingleProc` / `pickCommonProcs`). Patrz §7.

> **Rozstrzygnięte — semantyka genów dla podzbioru CDT/CGT:** przyjęto **(a) brute force wszystkich
> podzbiorów + scoring podzbioru per gen**, z wzorami:
> `Cheapest`→`koszt(S)`, `Fastest`→`czas(S)`, `MinTS`→`czas(S)·koszt(S)`,
> `LFU`→`Σ peUseCount[p]` po `S`, `Idle`→`min lastFinish[p]` po `S`,
> `SamePred`→reprezentant `k=1` (procesor rodzica; przy braku — fallback `Cheapest`).
> Zaimplementowane w `scoreCommonSubset` (`evaluator.cpp`). Patrz §6.4.

- [ ] **`reserved` w `@proc` i repetycja zadań** — nieużywane / niemodelowane (pytanie do prowadzącego,
  patrz `Input_format.md`). Trzymać do wyjaśnienia.

---

## 15. Prompt dla agenta Cursor (skrót)

```
Implementuj / rozwijaj solver GA dla ETG według ETG_GA_Design_v2.md:
- genotyp: SpanningTree (deterministyczne, STAŁE) + PeGene/ClsGene per taskId
- evaluator: topo order, model common 1/k, CLS na odbiorcy, fitness cost + Tmax  [gotowe]
- geny: wagi §4.3 w genes.cpp (discrete_distribution), kierunek idle §4.1  [gotowe]
- warstwa ewolucyjna:
    - bazowa (§9): tournament + Pc/Pm + elityzm + subtree crossover  [gotowe]
    - rozszerzona (§13): POP = round(alpha*n*tau), rank selection, frakcje beta/gamma/delta (=1),
      klonowanie, dynamiczny stop (noImproveLimit) — runGaExtended + flagi CLI  [gotowe]
- nie używaj gp_tree / reguły priorytetu
- parser i prepare() już istnieją
```

---

*Ostatnia aktualizacja: v2 — status zsynchronizowany z kodem, wagi genów zdecydowane (do wpięcia),
jawna decyzja o stałym drzewie, dodany pełny opis schematu α/β/γ/δ + rank selection + dynamiczny stop
jako rozszerzenie warstwy ewolucyjnej (§13), rozbudowane pozycje otwarte (§14).*
