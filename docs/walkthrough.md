# Walkthrough

## Fast Path
1. Configure build.
2. Build `unit_tests`.
3. Run full tests or module-specific demo filters.

Linux/macOS:
```bash
cmake -S . -B build -DENABLE_TORCH=ON -DLIBTORCH_ROOT=/opt/libtorch
cmake --build build --config Release --target unit_tests
./build/tests/unit/unit_tests --gtest_filter="PedagogicalWalkthroughTest.*"
```

Windows:
```powershell
cmake -S . -B build -DENABLE_TORCH=ON -DLIBTORCH_ROOT=C:/deps/libtorch
cmake --build build --config Release --target unit_tests
.\build\tests\unit\Release\unit_tests.exe --gtest_filter="PedagogicalWalkthroughTest.*"
```

## Why
This keeps examples executable, reviewable, and version-locked to runtime internals.
