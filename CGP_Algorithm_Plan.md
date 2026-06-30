# Plan algorytmu — Constructive Genetic Programming dla ETG

> Dokument roboczy dla całego zespołu. Opisuje **całą wizję algorytmu**, którą
> będziemy kodować: od populacji startowej, przez regułę priorytetu i budowę
> harmonogramu, po krzyżowanie, mutację i selekcję. Każde pojęcie jest
> wyjaśnione „po ludzku" + w kontekście **naszego** zadania (ETG, produkcja
> samolotu). Czytaj po kolei — najpierw ogół, potem szczegóły.

---

## 1. Cel projektu (co właściwie robimy)

Polecenie:

> *Using the extended task graph as a specification for an aircraft production
> process, please optimize the allocation of tasks to resources using a
> constructive genetic programming approach. The algorithm should optimize the
> cost given a specific time constraint.*

Po naszemu:

- Mamy **graf zadań** (ETG) opisujący proces produkcji samolotu.
- Mamy **zasoby** (maszyny / stanowiska), które te zadania wykonują.
- Szukamy **przydziału zadań do zasobów** (kto co robi i w jakiej kolejności),
  który jest **jak najtańszy**, ale **mieści się w zadanym limicie czasu Tmax**.

To jest problem **NP-trudny** (mapping + scheduling), dlatego używamy
metaheurystyki ewolucyjnej — konkretnie **constructive genetic programming**.

**Najważniejsze rozróżnienie, które musi rozumieć cały zespół:**

| | Co to | Czy to optymalizujemy? |
|---|---|---|
| **Cel (fitness)** | koszt całego harmonogramu | TAK — minimalizujemy |
| **Ograniczenie** | czas (makespan) ≤ Tmax | warunek twardy, nie cel |
| **Reguła priorytetu** | sposób budowania harmonogramu | to ją ewoluujemy, ale to NIE cel |

Czyli: **oceniamy po koszcie** (z warunkiem czasu), a **ewolucja dobiera regułę**,
która buduje tani-i-mieszczący-się-w-czasie harmonogram.

---

## 2. Słowniczek pojęć (w NASZYM kontekście)

To jest fundament. Każdy w zespole musi rozumieć te słowa tak samo.

### Gen
**Jeden pojedynczy element zapisu rozwiązania.**
- U nas: **jeden węzeł drzewa-reguły** — albo funkcja (`+`, `×`, `min`...), albo
  liść (terminal: `cost`, `time`, stała `0.7`...). Drzewo składa się z wielu genów.

### Genotyp
**Cały zapis jednego osobnika.**
- U nas: **całe drzewo-reguła priorytetu** (wszystkie węzły razem).

### Reguła priorytetu (= „program" / drzewo wyrażenia)
**Drzewo, które ocenia każdy możliwy wybór liczbą.** To jest dokładnie ta rzecz,
którą ewolucja krzyżuje i mutuje. W „genetic **programming**" osobnik to właśnie
**drzewo wyrażenia** (program), a nie gotowe rozwiązanie ani sztywny wektor wag.
- U nas drzewo łączy cechy (terminale) operatorami `+ − × ÷ min max`, np.:
  ```
            ( + )
           /     \
        ( × )    ( × )
        /   \    /   \
     cost  −1.0 time −0.5
  ```
  co znaczy `priorytet = (cost·−1.0) + (time·−0.5)`. Ale dzięki drzewu ewolucja
  może odkryć **dowolny kształt** wzoru, np. `min(−cost, −time·buyCost)`, czego
  sztywny wektor wag nie wyraża.

### Priorytet (wynik reguły)
**Chwilowa liczba — ranking jednego wyboru** (para zadanie↔zasób). Służy tylko
do tego, by wybrać następny ruch konstruktora. Po wyborze **znika**. To NIE jest
koszt ani wynik końcowy — to narzędzie budowy.

### Osobnik
**Jedno kandydujące rozwiązanie.** = reguła (genotyp) + harmonogram, który ona
buduje + ocena tego harmonogramu (koszt, czas).

### Populacja
**Zbiór wielu osobników naraz** (np. 100 różnych reguł), które „rywalizują"
i ewoluują razem.

### Dekoder konstruktywny (serce metody)
**Procedura, która z reguły (genotyp) buduje harmonogram (fenotyp).** Idzie po
grafie krok po kroku, w każdym kroku pyta regułę o priorytety i wybiera następny
ruch. „Constructive" = budujemy rozwiązanie **stopniowo**, więc jest ono
**zawsze poprawne** (nigdy nie złamiemy zależności w grafie).

### Fenotyp
**To, co powstaje z genotypu po uruchomieniu dekodera** = konkretny harmonogram.

### Fitness (ocena)
**Liczba mówiąca, jak dobry jest osobnik.** U nas: koszt + kara za przekroczenie
Tmax. Im mniejszy fitness, tym lepiej.

### Mapa zależności (zapamiętaj!)

```
gen        → jeden węzeł drzewa            (funkcja × albo liść cost)
genotyp    → całe drzewo-reguła            (np. (+ (× cost −1) (× time −0.5)))
   │  (dekoder konstruktywny uruchamia regułę krok po kroku)
   ▼
fenotyp    → gotowy harmonogram             (przydział wszystkich zadań)
   │
   ▼
fitness    → koszt + kara za Tmax           (ocena osobnika)

osobnik    = drzewo-reguła + harmonogram + fitness
populacja  = wiele osobników (np. 100 drzew)
```

---

## 3. Przypomnienie inputu (na czym operujemy)

Parser (`parseETG` w `etg.cpp`) daje gotowy obiekt `ETG`. Najważniejsze elementy,
o których algorytm MUSI pamiętać:

### 3.1. Zadania i ich TYPY (kluczowe — zachowują się różnie!)

Każde zadanie ma **kategorię** zakodowaną w prefiksie id (`GT0`, `UT2`, `CDT3`...):

| Typ | Prefiks | Na czym może działać | Ile zasobów naraz |
|---|---|---|---|
| General Task | `GT` | jeden **dowolny** zasób | 1 |
| Dedicated Task | `DT` | jeden **wyspecjalizowany** (sentinele mogą zawęzić) | 1 |
| Universal Task | `UT` | jeden **uniwersalny** | 1 |
| Common Dedicated Task | `CDT` | kilka **wyspecjalizowanych** jednocześnie | **k ≥ 1 — wybiera algorytm** |
| Common General Task | `CGT` | kilka **dowolnych** jednocześnie | **k ≥ 1 — wybiera algorytm** |

> **Najważniejsza specyfika ETG:** zadania **common (CDT/CGT) można podzielić na
> kilka zasobów** — część pracy robi jeden zasób, część drugi, równolegle.
> Liczba zasobów `k` NIE jest w pliku — **to algorytm decyduje**, na ile zasobów
> rozbić takie zadanie. To jest część rozwiązania, więc musi siedzieć w genomie.

### 3.2. Zasoby (też dwa różne typy)

`@proc` — każdy zasób ma koszt zakupu + flagę typu:

| Typ | Flaga | Zachowanie |
|---|---|---|
| **Uniwersalny** | `1` | może wykonać **wiele zadań** po kolei (sekwencyjnie). Tańszy/wolniejszy. |
| **Wyspecjalizowany** | `0` | wykonuje **dokładnie JEDNO** zadanie w całym harmonogramie. Szybki/drogi. |

- **Koszt zakupu** (`procs[p].cost()`) płacimy **raz**, jeśli zasób wykona ≥1 zadanie.

### 3.3. Macierze czasu i kosztu

- `times[t][p]` = czas wykonania zadania `t` na zasobie `p`,
- `costs[t][p]` = koszt wykonania zadania `t` na zasobie `p`,
- **Sentinel `< 0`** = ten zasób **NIE MOŻE** wykonać tego zadania (zakaz).

### 3.4. Komunikacja (`@comm`, opcjonalna)

- Krawędź grafu z `data > 0` = trzeba przesłać dane między zadaniami.
- Jeśli oba zadania są na **tym samym zasobie** → transfer **lokalny** (0 czasu, 0 kosztu).
- Inaczej → trzeba **kanału**, do którego oba zasoby są podłączone. Czas =
  `ceil(data / bandwidth)`, koszt podłączenia płacony raz na (kanał, zasób).

### 3.5. Tmax

- **Limit czasu (makespan)** — twarde ograniczenie. NIE jest w pliku ETG.
- Podajemy go jako **argument programu** (np. `--tmax N`).

### 3.6. Co algorytm dostaje na wejściu jednego rozwiązania (czego szuka)

1. Dla zadań **GT/DT/UT** → **jeden** dozwolony zasób.
2. Dla zadań **CDT/CGT** → **zbiór** zasobów `S` (liczność `k ≥ 1`) — podział pracy.
3. Dla zasobów uniwersalnych z wieloma zadaniami → **kolejność** ich wykonywania.
4. Pośrednio: wybór kanału dla każdej krawędzi komunikacyjnej.

---

## 4. Ogólna wizja algorytmu (wysoki poziom)

Najpierw całość w pigułce, żeby każdy widział „las, nie drzewa":

```
1. Wczytaj graf (parseETG) + Tmax.
2. Przygotuj dane pomocnicze (listy dozwolonych zasobów, poprzednicy, topologia).
3. Stwórz losową POPULACJĘ reguł priorytetu (np. 100 sztuk).
4. Powtarzaj przez wiele POKOLEŃ:
     a) Dla każdej reguły: DEKODER buduje harmonogram → policz (koszt, czas).
     b) Oceń każdą regułę: fitness = koszt + kara za przekroczenie Tmax.
     c) SELEKCJA: wybierz najlepsze reguły na rodziców.
     d) CROSSOVER: skrzyżuj rodziców → nowe reguły.
     e) MUTACJA: lekko zmień część nowych reguł.
     f) ELITYZM: przenieś najlepszą regułę bez zmian.
     g) Nowe pokolenie zastępuje stare.
5. Zwróć najlepszy znaleziony harmonogram (przydział + koszt + czas).
```

Hasło do zapamiętania:

> **Ewolucja dobiera regułę → reguła buduje harmonogram → koszt i czas oceniają
> regułę → powtarzasz.**

---

## 5. Szczegółowy opis każdego kroku

### KROK 0 — Wczytanie i przygotowanie danych (raz, przed ewolucją)

1. `parseETG(path)` → obiekt `ETG` (mamy gotowe).
2. `validateETG(g)` → jeśli błędy, przerwij; ostrzeżenia wypisz i jedź dalej.
3. Wczytaj **Tmax** z argumentu programu.
4. Policz raz:
   - `preds = buildPredecessors(g)` — poprzednicy każdego zadania,
   - `topo = topoOrder(g)` — porządek topologiczny (kolejność budowy),
   - dla **każdego zadania** listę **dozwolonych zasobów** `allowed[t]`
     = wszystkie `p`, gdzie spełniony filtr kategorii (GT/DT/UT/CDT/CGT)
     **oraz** `times[t][p] ≥ 0` i `costs[t][p] ≥ 0` (brak sentinela).
     > Uwaga implementacyjna: logika dozwolonych zasobów (`assignmentAllowed`)
     > jest dziś prywatna w `etg.cpp` — trzeba ją **wyeksponować** dla solvera.
5. (Opcjonalnie) dla każdej pary zasobów najtańszy wspólny kanał — przyspiesza
   liczenie komunikacji.

---

### KROK 1 — Reprezentacja osobnika: DRZEWO (czym jest genotyp)

> To jest sedno „genetic **programming**": osobnik to **drzewo wyrażenia**
> (program), a NIE sztywny wektor wag. Dzięki temu ewolucja odkrywa nie tylko
> „ile waży koszt", ale **cały kształt wzoru** na priorytet.

Osobnik = **drzewo-reguła priorytetu**. Drzewo, po obliczeniu dla pary
(zadanie `t`, zasób `p`), zwraca jedną liczbę = **priorytet** tego wyboru.

**Z czego zbudowane jest drzewo (dwa rodzaje węzłów):**

1. **Węzły wewnętrzne — funkcje (function set):**
   `+`, `−`, `×`, `÷` (zabezpieczone: dzielenie przez 0 → 1), `min`, `max`.
   Opcjonalnie `if>` (warunek). Każda funkcja ma dzieci (1–2 poddrzewa).

2. **Liście — terminale (terminal set):** cechy decyzji + stałe.

**Terminale — cechy liczone w momencie decyzji dla pary (`t`, `p`):**

| Terminal | Skąd | Znaczenie |
|---|---|---|
| `cost` | `costs[t][p]` | koszt wykonania zadania na zasobie |
| `time` | `times[t][p]` | czas wykonania |
| `buyCost` | `procs[p].cost()` | koszt zakupu zasobu (raz) |
| `nSucc` | graf (`successors`) | jak bardzo zadanie blokuje inne |
| `freeAt` | stan harmonogramu | kiedy zasób będzie wolny |
| stała | losowa (np. `0.7`, `−1.0`) | „pokrętło" do skalowania (ephemeral random constant) |

**Przykład genotypu (drzewo):**

```
            ( + )
           /     \
        ( × )    ( min )
        /   \     /    \
     cost  −1.0  −time  −buyCost
```

znaczy: `priorytet = (cost·−1.0) + min(−time, −buyCost)`.

**Domknięcie (closure) — ważna zasada projektowa:**
- Każda funkcja musi przyjąć wynik **dowolnej** innej (wszystko zwraca liczbę),
  a `÷` musi być **zabezpieczone** (dzielenie przez 0 nie wywala programu).
- Dzięki temu **każde** wygenerowane drzewo jest poprawnym, policzalnym wzorem.

**Wystarczalność (sufficiency):**
- Zbiór terminali musi pozwolić wyrazić sensowne heurystyki — stąd `cost`,
  `time`, `buyCost`, `nSucc`, `freeAt` (koszt, czas, zakup, struktura grafu, stan).

**Kontrola rozmiaru drzewa (bloat):**
- Drzewa GP mają tendencję do niekontrolowanego rozrostu („bloat"). Ograniczamy
  to **maksymalną głębokością** (np. 5–7) lub **maks. liczbą węzłów**, ewentualnie
  drobną karą za rozmiar w fitnessie.

> **Uwaga o znaku:** koszt/czas chcemy **minimalizować**, więc w sensownych
> drzewach pojawią się przy nich ujemne stałe lub odejmowanie — ale tego NIE
> wymuszamy z góry; ewolucja sama dobiera znaki, bo to część kształtu wzoru.

---

### KROK 2 — Populacja startowa (losowe drzewa)

1. Ustal rozmiar populacji `POP` (np. 50–200) i maks. głębokość startową (np. 2–6).
2. Wygeneruj `POP` **losowych drzew** — rekomendowana metoda **ramped
   half-and-half** (standard w GP):
   - **połowa drzew „full"** — wszystkie gałęzie rosną do maks. głębokości
     (węzły wewnętrzne to funkcje, dopiero liście to terminale),
   - **połowa drzew „grow"** — gałęzie mogą się kończyć wcześniej (na danym
     poziomie losowo wybierasz funkcję albo terminal),
   - „ramped" = robisz to dla **różnych głębokości** (np. 2,3,4,5,6) po równo.
3. Efekt: różnorodne kształty i rozmiary wzorów — jedne proste (`−cost`), inne
   złożone (`min(−time, −cost·buyCost)`). Ta różnorodność to paliwo dla ewolucji.

Przykład populacji (każdy wiersz = jeden osobnik = jedno drzewo):

| Osobnik | drzewo (wzór) | charakter |
|---|---|---|
| 1 | `−cost` | skrajnie tani |
| 2 | `(× −time 2.0)` | goni za czasem |
| 3 | `(+ −cost (× −time 0.5))` | zrównoważony |
| 4 | `(min −cost (− −time buyCost))` | złożony kompromis |
| … | … | … |

---

### KROK 3 — Dekoder konstruktywny (reguła → harmonogram) **[SERCE]**

To uruchamiamy dla **każdego** osobnika, żeby policzyć (koszt, czas).

**Stan początkowy:**
- `finish[t]` = nieustalony dla wszystkich zadań,
- `freeAt[p] = 0` dla każdego zasobu (kiedy wolny),
- `usedProcs = {}`, `burnedSpecialized = {}`, `channelConnections = {}`,
- `ready = {zadania bez poprzedników}`.

**Pętla — dopóki są niezaplanowane zadania:**

1. Dla każdego zadania `t ∈ ready` i każdego dozwolonego `p ∈ allowed[t]`:
   **policz priorytet** = reguła osobnika na cechach tej pary.
2. **Wybierz** parę (zadanie, zasób) o **najwyższym priorytecie**.
3. Policz **najwcześniejszy start**:
   ```
   start = max( gotowość_danych,   // wszyscy poprzednicy skończeni + transmisja
                freeAt[p] )        // zasób uniwersalny zajęty sekwencyjnie
   ```
4. **Obsłuż typ zadania** (TU różne typy zachowują się różnie):

   - **GT / DT / UT (jeden zasób):**
     - `czas = times[t][p]`, `koszt = costs[t][p]`.
     - Pamiętaj filtry: UT tylko uniwersalny, DT/CDT tylko wyspecjalizowany —
       to już jest w `allowed[t]`.

   - **CDT / CGT (zadanie common — podział na kilka zasobów):**
     - Dekoder wybiera **zbiór** `S ⊆ allowed[t]`, `k = |S| ≥ 1`, korzystając z
       priorytetów policzonych przez drzewo: np. „weź wszystkie zasoby z
       priorytetem powyżej progu" albo „weź `k` najlepszych" (patrz decyzja 4
       w sekcji 7). `k` NIE jest osobnym genem — wynika z reguły-drzewa.
     - Każdy zasób robi **1/k pracy**:
       ```
       czas  = Σ (po p∈S) time[t][p] / k
       koszt = Σ (po p∈S) cost[t][p] / k
       ```
     - **Dlaczego to się opłaca:** różne zasoby mają różne wpisy — można dobrać
       szybsze/tańsze do udziałów i lekko zbić czas/koszt względem jednego zasobu.

5. **Policz komunikację** dla każdej krawędzi `poprzednik → t` z `data > 0`:
   - ten sam zasób → 0 (lokalnie),
   - inny zasób → kanał, `czas = ceil(data / bandwidth)`, dolicz `connectCost`
     raz na (kanał, zasób); brak wspólnego kanału → harmonogram nielegalny (kara),
   - **dla wyjścia z common:** dane też dzielą się na `k` kawałków — każdy zasób
     z `S` wysyła `data/k` (czas `ceil((data/k)/b)`, koszt podłączenia per zasób).
6. **Zaktualizuj stan:**
   - `finish[t] = start + czas`,
   - `freeAt[p] = finish[t]` dla użytych zasobów uniwersalnych,
   - zasoby **wyspecjalizowane** → dopisz do `burnedSpecialized` (mogą zrobić
     tylko JEDNO zadanie — usuń je z `allowed` w kolejnych krokach),
   - dodaj użyte zasoby do `usedProcs`.
7. **Odśwież `ready`:** usuń `t`, dodaj zadania, których wszyscy poprzednicy mają
   już `finish`.

**Po zakończeniu pętli:**
```
makespan = max (po wszystkich t) finish[t]
koszt    = Σ koszt wykonania zadań
         + Σ buyCost[p] dla p ∈ usedProcs        // zakup zasobów
         + Σ koszty podłączeń kanałów
```

> **Determinizm:** ten sam genotyp musi zawsze dawać ten sam harmonogram —
> inaczej fitness „pływa" i ewolucja się rozjeżdża.

---

### KROK 4 — Fitness (ocena osobnika)

Cel = koszt, Tmax = warunek twardy. Wzór:

```
jeśli makespan ≤ Tmax:   fitness = koszt
jeśli makespan > Tmax:   fitness = koszt + λ · (makespan − Tmax)
```

- `λ` — duża kara (może rosnąć z pokoleniami). Dzięki niej rozwiązanie
  mieszczące się w czasie **zawsze** wygrywa z przekraczającym.
- Harmonogramy **strukturalnie nielegalne** (brak kanału itp.) → bardzo duża kara.
- Mniejszy fitness = lepszy osobnik.

> **Dlaczego reguła patrzy też na czas, skoro cel to koszt?** Bo najtańsze wybory
> są zwykle wolne → przekraczają Tmax → odrzucone. Reguła musi **ważyć koszt
> przeciw czasowi**, żeby w ogóle wyprodukować dopuszczalny harmonogram. Tej wagi
> szuka ewolucja.

---

### KROK 5 — Selekcja (kto zostaje rodzicem)

- **Turniejowa** (prosta, odporna): losuj `k` osobników, wygrywa ten z najniższym
  fitness; powtarzaj, aż wybierzesz rodziców.
- Alternatywa: rankingowa.
- (Rozszerzenie) **feasibility-first** (reguła Deba): dopuszczalne zawsze biją
  niedopuszczalne; wśród dopuszczalnych decyduje koszt.

---

### KROK 6 — Crossover (subtree crossover — krzyżowanie drzew)

Tworzymy nowe drzewa-reguły z dwóch rodziców przez **wymianę poddrzew**:

1. W rodzicu A wylosuj jeden węzeł (korzeń wycinanego poddrzewa).
2. W rodzicu B wylosuj jeden węzeł.
3. Dziecko = kopia A, w której wylosowane poddrzewo **podmieniono** na poddrzewo
   z B.

```
rodzic A:  (+ cost (× time 0.5))
rodzic B:  (× buyCost nSucc)
               ↓ wymień losowe poddrzewa ↓
dziecko :  (+ cost (× buyCost nSucc))
```

- Sens: dziecko dziedziczy „dobre kawałki" wzoru obu rodziców.
- Po crossover **sprawdź limit głębokości** — jeśli dziecko za głębokie, przytnij
  lub odrzuć (ochrona przed bloatem).

---

### KROK 7 — Mutacja (drobna losowa zmiana drzewa)

Z małym prawdopodobieństwem zmieniamy fragment drzewa. Typy mutacji w GP:

- **Subtree mutation:** wylosuj węzeł i podmień całe jego poddrzewo na **nowe
  losowe** poddrzewo.
  ```
  przed:  (+ cost (× time 0.5))
  po:     (+ cost (min buyCost time))   ← podmienione poddrzewo
  ```
- **Point mutation:** zmień **pojedynczy węzeł** na inny tego samego typu
  (funkcję na funkcję, np. `+`→`max`; terminal na terminal, np. `cost`→`time`;
  stałą na inną liczbę).

> **Uwaga — podział zadań common:** liczność `k` i zbiór zasobów `S` dla CDT/CGT
> NIE są kodowane w drzewie — wynikają z reguły w trakcie dekodowania (np. „bierz
> wszystkie zasoby z priorytetem powyżej progu"). Tę decyzję podejmuje dekoder
> (KROK 3, pkt 4), więc nie ma osobnego operatora mutacji na `k`.

- Sens: mutacja wprowadza nowość, chroni przed utknięciem.
- Po mutacji **pilnuj limitu głębokości** (bloat).

---

### KROK 8 — Elityzm

- Przenieś **najlepszego osobnika** (najniższy fitness) do nowego pokolenia
  **bez zmian**.
- Sens: gwarancja, że nie zgubimy najlepszego znalezionego rozwiązania.

---

### KROK 9 — Pętla i warunek stopu

- Powtarzaj KROKI 3–8 przez `G` pokoleń.
- **Stop, gdy:** osiągnięto `G` pokoleń **lub** brak poprawy najlepszego
  fitness przez `N` pokoleń (stagnacja).

---

### KROK 10 — Wynik

Zwróć **najlepszy znaleziony harmonogram**:
- przydział każdego zadania do zasobu/zasobów (dla common — zbiór `S` i `k`),
- kolejność na zasobach uniwersalnych,
- **całkowity koszt** i **makespan**,
- informację, czy spełniono Tmax.

(Opcjonalnie) uruchom na kilku różnych instancjach ETG — pokazuje, że podejście
czyta dowolny graf, nie tylko jeden.

---

## 6. Mini-przykład (intuicja na `input.txt`)

Pierwsza decyzja: gotowe tylko `T0` (jedyny korzeń). `T0` to `GT` → dowolny zasób.
Dane T0: 

| zasób | time | cost | buyCost |
|---|---|---|---|
| P0 | 30 | 3 | 100 |
| P1 | 10 | 2 | 200 |
| P2 | 3 | 50 | 500 |
| P3 | 4 | 10 | 300 |

Weźmy osobnika — drzewo `(− (− −cost (× time 0.5)) (× buyCost 0.01))`, czyli
`priorytet = −cost − 0.5·time − 0.01·buyCost`:
- P0: −19.0, **P1: −9.0 (max)**, P2: −56.5, P3: −15.0 → wybór **P1**.

Konstruktor przydziela `T0 → P1`, aktualizuje stan, przechodzi dalej (T1, T2...),
aż zbuduje cały harmonogram → liczy koszt i makespan → to jest fitness osobnika.
Inne drzewo (np. z `(× time 2.0)` zamiast `0.5`) wybrałoby P2 (time=3) — i to
właśnie różne **kształty drzew** przeszukuje ewolucja.

---

## 7. Otwarte decyzje (do ustalenia z zespołem / prowadzącym)

1. **Function set / terminal set:** czy zostajemy przy `+ − × ÷ min max` i
   terminalach `cost, time, buyCost, nSucc, freeAt`, czy dokładamy więcej cech
   (np. głębokość w grafie, długość ścieżki krytycznej)?
2. **Limit drzewa:** jaka maks. głębokość/liczba węzłów (kontrola bloatu)?
3. **Obsługa Tmax:** kara (start) czy feasibility-first (ulepszenie)?
4. **Jak reguła wybiera `k` i zbiór `S`** dla zadań common — „bierz wszystkie
   zasoby z priorytetem powyżej progu" czy „bierz `k` najlepszych" (i jak ustalić
   próg/`k`)? To decyzja w dekoderze, nie w drzewie.
5. **Powtarzalność zadań** z ogólnej specyfikacji ETG — na razie pomijamy
   (otwarte pytanie do prowadzącego).
6. **Kolumna `reserved`** w `@proc` — zakładamy 0.

---

## 8. Podsumowanie (jedno spojrzenie na całość)

```
INPUT (parser)         → graf zadań + zasoby + macierze + Tmax
   │
PRZYGOTOWANIE          → dozwolone zasoby, poprzednicy, topologia
   │
POPULACJA STARTOWA     → wiele losowych DRZEW-REGUŁ (ramped half-and-half)
   │
┌─ POKOLENIE ──────────────────────────────────────────────┐
│  dla każdej reguły:                                       │
│     DEKODER KONSTRUKTYWNY → buduje harmonogram krok-po-   │
│       kroku (typy zadań, podział common na k zasobów,     │
│       komunikacja) → (koszt, makespan)                    │
│     FITNESS = koszt + kara za przekroczenie Tmax          │
│  SELEKCJA → CROSSOVER → MUTACJA → ELITYZM                 │
└───────────────────────────────────────────────────────────┘
   │  (powtarzaj aż do G pokoleń / stagnacji)
   ▼
WYNIK                  → najtańszy harmonogram mieszczący się w Tmax
```

**Najkrótsze ujęcie:** *ewolucja dobiera regułę priorytetu w postaci **drzewa
wyrażenia** → konstruktywny dekoder używa tego drzewa, żeby krok po kroku
zbudować zawsze legalny harmonogram (z poszanowaniem typów zadań i podziału zadań
common na kilka zasobów) → oceniamy go kosztem przy twardym limicie czasu Tmax →
najlepsze drzewa przeżywają, **krzyżują się przez wymianę poddrzew i mutują przez
podmianę poddrzew/węzłów** → po wielu pokoleniach zostaje najtańszy dopuszczalny
harmonogram.*
