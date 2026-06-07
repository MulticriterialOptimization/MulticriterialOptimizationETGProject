# MulticriterialOptimizationETG

Optimization of task-to-resource allocation using Extended Task Graph (ETG).
Goal: minimize cost subject to a makespan time constraint, using a genetic algorithm.

## Structure

- `etg.h` / `etg.cpp` — data model, parser, validation
- `MulticriterialOptimizationETGProject.cpp` — entry point
- `input.txt` — sample ETG instance
- `NOTATKA_ETG.md` — format documentation and open questions for the instructor

## Build & Run

### Visual Studio (Windows)

1. Open `MulticriterialOptimizationETGProject.slnx`
2. Place `input.txt` next to `.vcxproj` (default working directory)
3. **Ctrl+F5**

To use a different file: Project → Properties → Debugging → Command Arguments.

### CMake (Windows / macOS / Linux)

```
cmake -B build
cmake --build build
```

Windows: `.\build\Debug\MulticriterialOptimizationETG.exe input.txt`  
macOS/Linux: `./build/MulticriterialOptimizationETG input.txt`