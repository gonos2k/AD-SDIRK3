# Memory capture path preflight correction

Local timestamp: 2026-10-09T19:50:32.945219+09:00.

Run 37919251547 stopped at scratch compilation, before any model step or Valgrind model capture. ZIP SHA-256 6834c1e1172adb7fb4adb64b18f088d84170b5c17de7aaa60933a7e55543858d is verified. The error was a relative scratch-source path interpreted against the CMake build working directory.

The helper now resolves its output directory before materializing compiler arguments. Green verified actual local Clang scratch compilation and linking with a relative output-directory argument, matching the existing archive/flags and original quoted headers. The local report explicitly records a harness-only platform-check bypass and mocked Valgrind commands: no model executable was launched and this is not a memory result. The temporary compile database was removed; original core archive and authoritative source stayed unchanged.

Next is one corrected Linux three-step Valgrind capture. The prior failed artifact is preserved; no identical retry, long forecast, production change, WRF/RK3 comparison or merge was performed. Gradient accuracy, time accuracy and retained allocation ownership remain open.
