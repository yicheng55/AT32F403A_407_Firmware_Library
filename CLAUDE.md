# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

This is the AT32F403A/407 Firmware Library v2.2.1 from ArteryTek - a comprehensive peripheral library for AT32F403A and AT32F407 ARM Cortex-M4 microcontrollers. The library follows a Hardware Abstraction Layer (HAL) approach similar to STM32 libraries.

## Architecture

### Directory Structure

- **`libraries/`** - Core firmware library
  - `drivers/` - Peripheral drivers (26 header files covering all MCU peripherals)
    - `inc/` - Driver header files (e.g., `at32f403a_407_rtc.h`, `at32f403a_407_gpio.h`)
    - `src/` - Driver implementation files
  - `cmsis/` - ARM CMSIS support for Cortex-M4 core
    - `cm4/core_support/` - Core ARM CMSIS files
    - `cm4/device_support/` - AT32-specific device files

- **`project/`** - Development projects and examples
  - `at_start_f403a/` - AT32F403A development board projects
  - `at_start_f407/` - AT32F407 development board projects
  - `at32f403a_407_board/` - Board support package (BSP)

- **`middlewares/`** - Middleware components (USB, etc.)
- **`utilities/`** - Utility functions and helpers
- **`document/`** - Documentation files

### Project Templates and Examples

Each chip variant has template projects under `project/[board]/templates/` with support for multiple IDEs:
- MDK-ARM (Keil v4/v5)
- IAR EWARM (v6.10, v7.4, v8.2, v9.3)
- Eclipse GCC
- AT32 IDE

Examples are organized by peripheral under `project/[board]/examples/[peripheral]/[feature]/`.

## Development Workflow

### IDE Project Structure
- Each example/template contains multiple IDE-specific folders (e.g., `mdk_v5/`, `iar_v7.4/`)
- Project files: `.uvprojx` (Keil), `.eww/.ewp` (IAR)
- Common source structure: `src/`, `inc/`, shared across IDE variants

### Working with Examples
1. Navigate to `project/at_start_f403a/examples/[peripheral]/[example]/`
2. Open the appropriate IDE folder
3. Each example includes a `readme.txt` explaining the functionality

### Driver Usage Pattern
- Include main driver header: `#include "at32f403a_407_[peripheral].h"`
- Peripheral drivers follow consistent naming: `[peripheral]_[function]()` (e.g., `rtc_counter_set()`)
- Configuration structures: `[peripheral]_init_type` (e.g., `rtc_init_type`)

### Board Support Package (BSP)
Located in `project/at32f403a_407_board/`:
- `at32f403a_407_board.h/.c` - Board-specific definitions and functions
- Provides abstraction for LEDs, buttons, and other board peripherals

## Key Conventions

### Naming Conventions
- Files: `at32f403a_407_[peripheral].[h/c]`
- Functions: `[peripheral]_[action]_[object]()`
- Constants: `[PERIPHERAL]_[DESCRIPTION]_[TYPE]` (e.g., `RTC_TS_FLAG`)
- Structures: `[peripheral]_[purpose]_type`

### Flag and Register Patterns
- Status flags: `[PERIPHERAL]_[NAME]_FLAG` with hex values
- Register bits follow consistent bit field definitions
- Enable/disable functions: `[peripheral]_enable()`, `[peripheral]_disable()`

## License
BSD 3-Clause License (see LICENSE file)

## Reference Documentation
- HTML example lists: `project/at_start_f403a_Example_list.htm`, `project/at_start_f407_Example_list.htm`
- Driver release notes: `libraries/drivers/ReleaseNotes_AT32F403A_407_Firmware_Library_Drivers.pdf`
- Peripheral library documentation: `AT32F403A_407_periph_lib_V2.2.1.chm`