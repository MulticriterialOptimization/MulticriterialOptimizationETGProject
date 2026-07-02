# Projekt ETG — opis problemu i projekt algorytmu genetycznego

> Dokument roboczy dla zespołu. Opisuje **problem**, **model ETG**, **uzgodnione podejście ewolucyjne**
> (wg wykładu) oraz szczegóły implementacyjne wystarczające, by Cursor/agent mógł kodować
> bez domyślania kluczowej logiki.
>
> **Status:** specyfikacja zamknięta (oprócz wag prawdopodobieństw genów — do uzupełnienia).
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

Parser i walidacja ETG są gotowe (`src/etg.cpp`, `src/etg_prep.cpp`). Solver GA — do implementacji.

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
  drzewo rozpinające DAG-u  —  tylko struktura (crossover, „same as predecessor”)
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
- Drzewo służy m.in. genowi **„Same as predecessor”** (procesor rodzica **w drzewie**, nie dowolnego poprzednika w DAG).

**Budowa (deterministyczna, raz na instancję):**

```
dla każdego t w porządku topologicznym:
  parent[t] = poprzednik z preds[t] o najmniejszym id
  (brak poprzedników → parent[t] = -1, korzeń)
```

Komunikacja **nie jest** węzłem drzewa genotypu — powstaje w evaluatorze z przypisań + krawędzi ETG.

---

## 4. Zbiór genów (function set)

Gen = **wybór heurystyki** z poniższych list (enum), **nie** węzeł drzewa wyrażenia.

### 4.1 Genes PEs — przypisanie procesora (węzeł / task)

| Id | Nazwa (slajd) | Semantyka w ETG |
|---|---|---|
| `AllocCheapest` | Allocated → cheapest | Min koszt w `usedProcs ∩ allowed[t]` ∩ dostępnych |
| `AllocFastest` | Allocated → fastest | Min `times[t][p]` w allocated |
| `AllocLFU` | Allocated → LFU | Procesor z **najmniejszą liczbą użyć** w harmonogramie |
| `AllocIdle` | Allocated → idle longest | Procesor z **najdłuższym czasem od zakończenia ostatniego zadania** |
| `AllocSamePred` | Allocated → same as predecessor | Procesor **rodzica w drzewie rozpinającym** |
| `Cheapest` | Smallest area / cheapest (global) | Min koszt w `allowed[t]` ∩ dostępnych |
| `Fastest` | Fastest (global) | Min czas w legalnych i dostępnych |
| `MinTS` | Min(t×s) | Min `times[t][p] × costs[t][p]` |

**Mapowanie „cheapest” (PE):**

- koszt wykonania: `costs[t][p]`,
- przy **pierwszym** użyciu procesora `p` dolicz `procs[p].cost()` (buyCost).

**„Allocated” (PE):** procesory **już użyte** w bieżącym harmonogramie (`usedProcs`),
po filtracji przez `allowed[t]` i dostępność (patrz §6).

### 4.2 Genes CLS — wybór kanału (węzeł / task — odbiorca)

Stosowany przy każdej krawędzi ETG `u → t` z `data > 0`, gdy `proc(u) ≠ proc(t)`.
Reguła brana z **`ClsGene[t]`** (węzeł odbiorcy).

| Id | Nazwa (slajd) | Semantyka w ETG |
|---|---|---|
| `ClsAllocCheapest` | Allocated → cheapest | Min `connectCost` w kanałach już podłączonych |
| `ClsAllocFastest` | Allocated → fastest | Max `bandwidth` w allocated |
| `ClsAllocLFU` | Allocated → LFU | Kanał z najmniejszą **liczbą użyć** |
| `ClsAllocIdle` | Allocated → idle longest | Kanał z najdłuższym czasem od **zakończenia ostatniej** transmisji |
| `ClsCheapest` | Smallest area / cheapest | Min `connectCost` spośród legalnych |
| `ClsHighestBw` | Highest bandwidth | Max `bandwidth` spośród legalnych |
| `ClsLFU` | Least frequently used | Globalnie najrzadziej używany legalny kanał |

**„Allocated” (CLS):** kanały, do których **już podłączono procesor** w tym harmonogramie
(`connectCost` za tę parę (kanał, procesor) został już zapłacony / kanał „aktywny”).

**Legalny kanał** dla `(ps, pr)`: oba procesory mają `canConnect == 1`.

### 4.3 Prawdopodobieństwa

- PEAllocated 60%
     - AllocCheapest 20%
     - AllocFastest 20%
     - AllocLFU 20%
     - AllocIdle 20%
     - AllocSamePred 20%
- PECheapest 10%
- PEFastest 10%
- PEMin(time*cost) 20%

- ClsAllocated 60%
  - ClsAllocCheapest 20%
  - ClsAllocFastest 20%
  - ClsAllocLFU 30%
  - ClsAllocIdle 30%
- ClsCheapest 10%
- ClsHighestBw 10%
- ClsLFU 20%
  

---

## 5. Reprezentacja osobnika (propozycja kodu)

```cpp
enum class PeGene { AllocCheapest, AllocFastest, AllocLFU, AllocIdle, AllocSamePred,
                    Cheapest, Fastest, MinTS };

enum class ClsGene { ClsAllocCheapest, ClsAllocFastest, ClsAllocLFU, ClsAllocIdle,
                     ClsCheapest, ClsHighestBw, ClsLFU };

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

Istniejący `gp_tree` (reguła priorytetu) **nie jest** częścią tego podejścia — może zostać w repo (testy), solver idzie w nowych plikach.

---

## 6. Evaluator — budowa harmonogramu

### 6.1 Kolejność

Zadania planowane w **`PreparedData::topo`** (porządek topologiczny **całego DAG**, nie DFS drzewa genotypu).

### 6.2 Stan evaluatora (śledzony w trakcie)

| Pole | Znaczenie |
|---|---|
| `freeAt[p]` | kiedy procesor `p` będzie wolny (uniwersalne) |
| `lastFinish[p]` | czas zakończenia ostatniego zadania na `p` (idle PE) |
| `peUseCount[p]` | LFU procesorów |
| `usedProcs` | zbiór procesorów, które wykonały ≥1 zadanie |
| `burnedSpecialized` | wyspecjalizowane już zajęte |
| `channelConnected[c][p]` | czy procesor `p` podłączony do kanału `c` |
| `channelUseCount[c]` | LFU kanałów |
| `channelLastFinish[c]` | idle kanałów |
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
     - LFU / idle / same-as-pred — zastosuj regułę do wyboru **reprezentanta** lub pierwszego elementu, potem ewaluuj podzbiory zawierające wynik reguły *(implementacja może: pełny brute force podzbiorów + scoring wg genu)*.
3. Wybierz **najlepszy podzbiór** `S` według kryterium genu.
4. Przypisz wszystkie procesory z `S`.

**Uwaga złożoności:** do `2^|allowed[t]|` podzbiorów — OK dla typowych instancji projektu.

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
| **Same as predecessor** niedostępny / niellegalny | fallback z uwzględnieniem **czasu** (np. najbliższy dostępny minimalizujący opóźnienie — lub Cheapest spośród dostępnych) |
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

## 9. Operator genetyczny

### 9.1 Inicjalizacja

- `POP` osobników (np. 50–200, parametr).
- Dla każdego `t`: losowy `PeGene[t]` i `ClsGene[t]` (uniform, potem wagi).

### 9.2 Selekcja

- Turniejowa (np. k=3): wygrywa niższy fitness.

### 9.3 Crossover

- **Uniform per węzeł:** dla każdego `t` losowo weź `(PeGene, ClsGene)` od rodzica A lub B.
- Alternatywa: subtree crossover w **tym samym** węźle drzewa (kształt identyczny → bezpieczne).

### 9.4 Mutacja

- Losowy task `t` → nowy `PeGene[t]`.
- Losowy task `t` → nowy `ClsGene[t]`.
- Prawdopodobieństwo mutacji `Pm` (parametr).

### 9.5 Elitizm

- Kopiuj `E` najlepszych bez zmian (np. 1–5).

### 9.6 Parametry (domyślne do ustalenia w CLI)

`POP`, `GENERATIONS`, `Pc`, `Pm`, `ELITE`, `λ`, `--tmax`.

---

## 10. Przepływ programu

```
parseETG(path)
validateETG(graph)
prep = prepare(graph)           // allowed, preds, topo, nSucc
tree = buildSpanningTree(prep)  // deterministyczne

pop = initPopulation(POP, prep, tree)

for gen in 1..GENERATIONS:
    for each individual:
        evaluate(ind, graph, prep, tree, Tmax)  // fenotyp + fitness
    pop = select + crossover + mutate + elitism(pop)

output best individual's schedule
```

---

## 11. Pliki w repozytorium

| Plik | Rola |
|---|---|
| `src/etg.h`, `src/etg.cpp` | model ETG, parser, walidacja |
| `src/etg_prep.h`, `src/etg_prep.cpp` | `allowed`, `preds`, `topo` |
| `src/schedule.h` | `Schedule`, `Individual` — **do rozbudowy / nowe typy genów** |
| `src/gp_tree.*` | stary plan (reguła priorytetu) — **poza tym designem** |
| `CGP_Algorithm_Plan.md` | stary plan zespołu — **archiwum / nieaktualny** |
| **`ETG_GA_Design.md`** | **ten dokument — aktualna specyfikacja** |

Proponowane nowe pliki implementacyjne:

- `src/genes.h` — enumy `PeGene`, `ClsGene`
- `src/spanning_tree.h/.cpp` — budowa drzewa rozpinającego
- `src/evaluator.h/.cpp` — PE/CLS + harmonogram + fitness
- `src/ga.h/.cpp` — populacja, selekcja, crossover, mutacja

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

---

## 13. Otwarte / do uzupełnienia później

- [ ] Wagi prawdopodobieństw genów PE i CLS (obecnie uniform).
- [ ] Dokładna reguła fallback dla `AllocSamePred`, gdy poprzednik niedostępny (Cheapest vs min start time).
- [ ] Dla CDT/CGT: pełna semantyka genów LFU/idle/same-as-pred przy wyborze **podzbioru** (brute force + scoring wg genu — do doprecyzowania w kodzie z komentarzem).
- [ ] Limit podzbiorów przy bardzo dużym `|allowed[t]|` (opcjonalna optymalizacja).

---

## 14. Prompt dla agenta Cursor (skrót)

```
Implementuj solver GA dla ETG według ETG_GA_Design.md:
- genotyp: SpanningTree (deterministyczne) + PeGene/ClsGene per taskId
- evaluator: topo order, model common 1/k, CLS na odbiorcy, fitness cost + Tmax
- GA: init uniform, tournament, uniform crossover per node, mutation, elitism
- nie używaj gp_tree / reguły priorytetu
- parser i prepare() już istnieją
```

---

*Ostatnia aktualizacja specyfikacji: uzgodnienia zespołu (drzewo rozpinające, geny PE/CLS w węźle, fallbacki, CDT/CGT przez podzbiory, mapowanie kosztów).*
