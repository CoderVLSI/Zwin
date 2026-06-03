# ⚡ Zwin: AI-Native Scripting Language & VM

Zwin is an ultra-lightweight, memory-safe, interpreted scripting language and virtual machine (VM) designed specifically for **AI Agents (LLMs)** running on resource-constrained edge hardware like the **ESP32** (microcontrollers) and **Raspberry Pi Zero 2 W** (single-board computers).

---

## 🚀 Why Zwin?

1.  **AI-Generated Safety (The Sandbox):** Unlike compiled languages (C++/Rust) or native scripts (Python/Bash) where an LLM bug can cause a division-by-zero crash, stack overflow, or memory panic, the **Zwin VM catches all execution errors safely**, printing the trace back to the AI and keeping the drone/robot running.
2.  **Ultra-Lightweight Footprint:** The C++ Zwin VM runs in **under 2 KB of RAM** and consumes less than 15 KB of flash storage, outperforming general runtime environments.
3.  **On-the-Fly Execution:** Execute raw text scripts dynamically over Wi-Fi, Bluetooth, or Serial at runtime without compiling or rebooting the hardware.

---

## 📜 Language Specification & Syntax

Zwin features an event-driven, hardware-aware syntax that is simple for LLMs to generate:

```zwin
# Zwin Script: Obstacle Avoidance & Alert
call status_led.write(state=1)
var dist = call sonar.read()

if dist < 20 {
    call status_led.blink(count=5, delay_ms=100)
    call motors.stop()
    call speaker.say("Obstacle detected!")
} else {
    call motors.drive(left=150, right=150)
}
call status_led.write(state=0)
```

### Supported Flow Controls
*   `call <module>.<action>(<params>)` — Executes hardware bindings or external APIs.
*   `var <name> = <expr>` — Declares and updates local string/float variables.
*   `if <condition> { ... } else { ... }` — Evaluates logical comparisons.
*   `loop <count> { ... }` — Runs static loops with safe limits to prevent infinite locks.
*   `delay(<ms>)` — Pauses script execution.

---

## 📁 Repository Structure

*   `/zig_impl` — Native Zig implementation of the Zwin client interpreter (ideal for Pi Zero).
*   `/cpp_impl` — Portable C++ implementation of the Zwin VM and SDK bindings (compiled for Arduino/ESP32).

---

## ⚙️ How to Build & Run

### ⚡ 1. Zig Implementation (Raspberry Pi)
Requires Zig 0.11.0+. Clone and run the script:

```bash
cd zig_impl
export GEMINI_API_KEY="your_api_key"
zig run main.zig
```

To compile a stripped, statically-linked release binary under **1.5 MB**:
```bash
zig build-exe main.zig -O ReleaseSmall -fstrip
./main
```

### 🔌 2. C++ Implementation (ESP32 / Arduino)
Copy the files under `/cpp_impl` directly into your Arduino or PlatformIO project structure:

```cpp
#include "zwin_vm.h"

ZwinVM vm;

void setup() {
    vm.begin();
    // Register your hardware bindings here
    vm.registerFunction("status_led.write", your_led_func);
}

void loop() {
    if (Serial.available()) {
        String script = Serial.readString();
        String err;
        if (!vm.execute(script, err)) {
            Serial.printf("Zwin Error: %s\n", err.c_str());
        }
    }
}
```

---

## ⚖️ License

Distributed under the MIT License. See `LICENSE` for more information.
