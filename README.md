# AntiDouble

Vibe coded lightweight Windows utility to eliminate left mouse button double-click chatter and drag drop drops caused by worn-out microswitches.

Intercepts low-level mouse events through a global hook, measures intervals using high-resolution performance counters (QPC), drops chatter events below the defined threshold, and bridges contact dropouts while holding or dragging.

## Features

- Blocks phantom double clicks on button release and press
- Prevents accidental drop during file or window dragging
- Minimalist terminal logs with microsecond timer accuracy
- Zero background overhead

## Before and After

Before:

![Before](assets/before.png)

After:

![After](assets/after.png)

## Usage

Run the executable:

```cmd
AntiDouble.exe
```

Optional parameters:

```cmd
AntiDouble.exe [threshold_ms] [drag_debounce_ms]
```

Example (70ms double click threshold, 25ms drag hold bridge):

```cmd
AntiDouble.exe 70 25
```

Console output format:

```text
[+] double-click blocked (42ms)
[+] drag chatter blocked (11ms)
```

## Build

Compile with MSVC:

```cmd
cl /O2 /EHsc /std:c++17 main.cpp user32.lib
```
