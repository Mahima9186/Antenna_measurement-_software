# AMAS Project: Problems Faced & Solutions Log

This log chronicles the key technical challenges, diagnostics, and resolutions encountered throughout the development and integration of the Antenna Measurement Automation System (AMAS).

---

## 1. Compiler Toolchain & VISA Library Architecture Mismatch

### Problem Description
During early setup, building standalone C++ test programs to link with the National Instruments/Keysight VISA libraries (`visa64.lib`) failed.
* **Symptom:** Linking errors occurred when compiling via standard GCC/MinGW (32-bit), showing unresolved references to VISA C functions (`viOpen`, `viWrite`, etc.).
* **Root Cause:** The system's Keysight/NI VISA libraries are compiled for 64-bit architecture. Attempting to compile and link them using a legacy 32-bit MinGW GCC compiler led to binary incompatibility.

### Diagnostics & Resolution
* **Diagnostics:** Checked `CMAKE_SIZEOF_VOID_P` to confirm compiler bitness. Found that 32-bit compilers cannot resolve symbols in `C:/Program Files/IVI Foundation/VISA/Win64/Lib_x64/msc/visa64.lib`.
* **Resolution:**
  1. Updated [CMakeLists.txt](file:///c:/JV_Intern/AMAS/CMakeLists.txt) to automatically detect target bitness and choose the correct library paths (`visa32` for 32-bit, `visa64` for 64-bit).
  2. Documented and established the use of the **MSVC x64 Native Tools Developer Command Prompt** for standalone builds, running compilation through `cl.exe` to natively generate 64-bit executables.

---

## 2. VISA Instrument (VNA) Connection & Communication Failures

### Problem Description
Connecting to the Keysight Vector Network Analyzer (VNA) at address `GPIB0::18::INSTR` frequently failed with non-descript error exits, or commands succeeded silently but did not execute on the analyzer.
* **Symptom:** The test execution would exit on resource manager failures, or configuration requests (frequencies, span) were ignored by the instrument.

### Diagnostics & Resolution
* **Diagnostics:** 
  1. Added standard error stream (`std::cerr`) logging in C++ to capture and output raw VISA status codes (`ViStatus`) in hexadecimal.
  2. Implemented active error checking by writing the SCPI error query `:SYST:ERR?` after every configuration payload and reading the output buffer to capture hardware-level exceptions.
* **Resolution:** 
  * Implemented structured diagnostic warnings to prompt users to check physical connections, GPIB addresses, and device power when failures occur.
  * Corrected string command sequences in [visa_test.cpp](file:///c:/JV_Intern/AMAS/visa_test.cpp) to ensure proper termination characters (`\n`) are appended to SCPI commands to trigger parsing on the instrument side.

---

## 3. Qt6 Build Dependencies in Environment Without Qt Path Variables

### Problem Description
Adding Qt-based Modbus dependencies (`Qt6::SerialBus` and `Qt6::SerialPort`) broke the project's CMake configure step on standard CLI sessions.
* **Symptom:** Running `cmake --preset default` returned `Could not find a package configuration file provided by "Qt6"`.
* **Root Cause:** The system environment does not have Qt6 paths configured in the environment variables (`PATH` or `CMAKE_PREFIX_PATH`). Consequently, CMake is unable to resolve Qt6 package targets.

### Diagnostics & Resolution
* **Diagnostics:** Ran standard environment queries and verified that the Qt6 configuration directories were absent from local path variables.
* **Resolution:** Pivoted the physical serial positioner driver design to use the native **Windows Win32 Serial API** (`CreateFileA`, `SetCommState`, `ReadFile`, `WriteFile`) inside standard C++ code. This allowed the driver and testing utility to remain lightweight and compile in a pure C++ environment without any Qt package requirements.

---

## 4. TAP-3001 Serial Connection Timeout

### Problem Description
The initial compilation of `positioner_test.exe` opened `COM3` successfully, but attempting to read coordinates resulted in a timeout.
* **Symptom:** `[ERROR] Modbus Write Response: Read timeout. Expected 8 bytes, read 0.`
* **Root Cause:**
  1. **Baud Rate Mismatch:** The initial C++ code configured the COM port to `9600` baud. The operational manual for the TAP-3001 specifies that the default hardware serial configuration is `19200` baud.
  2. **RS-485 Signal Polarities:** The physical connection uses a USB-to-RS485 adapter, where inverted Data+ (A) and Data- (B) lines prevent packet transmission.

### Diagnostics & Resolution
* **Diagnostics:** Created a custom configuration scanner `modbus_scanner.cpp` to sweep through parities, stop bits, and common baud rates, verifying that no responses were registered.
* **Resolution:**
  1. Updated the connection parameter defaults to **`19200 8N1`** (no parity, 1 stop bit, 19200 baud) as detailed in the technical manual.
  2. Provided troubleshooting instructions to swap the **A** and **B** differential wires on the RS-485 adapter terminal block to correct signal polarity.

---

## 5. TAP-3001 Modbus Register Index Offset

### Problem Description
Even with the correct serial parameters, queries and target movements on the Azimuth axis were rejected or ignored.
* **Symptom:** Target write commands generated exceptions or were ignored by the positioner controller.
* **Root Cause:** Standard Modbus RTU implementation offsets registers by subtracting 1 (`address = startRegAddress - 1`). The TAP-3001, however, uses direct 0-based protocol addresses mapped 1-to-1 with its register numbers:
  * Register `3` maps directly to Protocol Address `3` (`AZCUR`).
  * Register `10` maps directly to Protocol Address `10` (`ELREF`).
  * Register `18` maps directly to Protocol Address `18` (`PLREF`).
  * Subtracting 1 in C++ resulted in reading from address `2` and writing to address `1`, which are wrong coordinate paths.

### Diagnostics & Resolution
* **Diagnostics:** Reviewed the working Python implementation (`Connectivity.py`) which utilizes `pymodbus` to directly communicate with protocol addresses `3`, `10`, and `18` with no offset subtraction.
* **Resolution:** Removed the `- 1` offset logic in both [positioner_test.cpp](file:///c:/JV_Intern/AMAS/positioner_test.cpp) and [TAP3001Driver.cpp](file:///c:/JV_Intern/AMAS/src/core/positioner/TAP3001Driver.cpp). Redefined all calls to target the exact raw 0-based protocol addresses:
  * **Azimuth:** Address `3` (for read and write)
  * **Elevation:** Address `10` (for read and write)
  * **Polarization:** Address `18` (for read and write)
  * **Speeds:** Addresses `38` (AZ), `46` (EL), `54` (PL)
