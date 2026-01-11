# Timer unit

## Description

BASIC TIMER component manages the following features:

- 2 general purpose 32bits up counter timers
- Input trigger sources:
    - FLL clock
    - FLL clock + Prescaler
    - Reference clock at 32kHz
    - External event
- 8bit programmable prescaler to FLL clock
- Counting modes:
    - One shot mode: timer is stopped after first comparison match
    - Continuous mode: timer continues counting after comparison match
    - Cycle mode: timer resets to 0 after comparison match and continues counting
    - 64 bit cascaded mode
- Interrupt request generation on comparison match

## Registers

| Register Name | Offset  | Width | Access Type | Reset Value | Description                           |
| ------------- | ------- | ----  | ----------- | ------------| ------------------------------------- |
| CFG_LO        | 0x0     | 32    | R/W         | 0x0         | Timer Low Configuration register.     |
| CFG_HI        | 0x4     | 32    | R/W         | 0x0         | Timer High Configuration register.    |
| CNT_LO        | 0x8     | 32    | R/W         | 0x0         | Timer Low counter value register.     |
| CNT_HI        | 0xC     | 32    | R/W         | 0x0         | Timer High counter value register.    |
| CMP_LO        | 0x10    | 32    | R/W         | 0x0         | Timer Low comparator value register.  |
| CMP_HI        | 0x14    | 32    | R/W         | 0x0         | Timer High comparator value register. |
| START_LO      | 0x18    | 32    | R/W         | 0x0         | Start Timer Low counting register.    |
| START_HI      | 0x1C    | 32    | R/W         | 0x0         | Start Timer High counting register.   |
| RESET_LO      | 0x20    | 32    | R/W         | 0x0         | Reset Timer Low counter register.     |
| RESET_HI      | 0x24    | 32    | R/W         | 0x0         | Reset Timer High counter register.    |

### CFG_LO

#### Fields

| Field Name   | Offset | Width | Access Type | Reset Value | Description |
| ---          | ---    | ---   | ---         | ---         | ---         |
| ENABLE       | 0      | 1     | R/W         | 0x0         | Timer low enable configuration bitfield: - 1'b0: disabled - 1'b1: enabled |
| RESET        | 1      | 1     | R/W         | 0x0         | Timer low counter reset command bitfield. Cleared after Timer Low reset execution. |
| IRQEN        | 2      | 1     | R/W         | 0x0         | Timer low compare match interrupt enable configuration bitfield: - 1'b0: disabled - 1'b1: enabled |
| IEM          | 3      | 1     | R/W         | 0x0         | Timer low input event mask configuration bitfield: - 1'b0: disabled - 1'b1: enabled |
| MODE         | 4      | 1     | R/W         | 0x0         | Timer low continuous mode configuration bitfield: - 1'b0: Continue mode - continue incrementing Timer low counter when compare match with CMP_LO occurs. - 1'b1: Cycle mode - reset Timer low counter when compare match with CMP_LO occurs. |
| ONE_S        | 5      | 1    | R/W          | 0x0         | Timer low one shot configuration bitfield: - 1'b0: let Timer low enabled counting when compare match with CMP_LO occurs. - 1'b1: disable Timer low when compare match with CMP_LO occurs. |
| PEN          | 6      | 1    | R/W          | 0x0         | Timer low prescaler enable configuration bitfield:- 1'b0: disabled - 1'b1: enabled |
| CCFG         | 7      | 1    | R/W          | 0x0         | Timer low clock source configuration bitfield: - 1'b0: FLL or FLL+Prescaler - 1'b1: Reference clock at 32kHz |
| PVAL         | 8      | 8    | R/W          | 0x0         | Timer low prescaler value bitfield. Ftimer = Fclk / (1 + PRESC_VAL) |
| CASC         | 31     | 1    | R/W          | 0x0         | Timer low  + Timer high 64bit cascaded mode configuration bitfield. |

### CFG_HI

#### Fields

| Field Name   | Offset | Width | Access Type | Reset Value | Description |
| ---          | ---    | ---   | ---         | ---         | ---         |
| ENABLE       | 0      | 1     | R/W         | 0x0         | Timer high enable configuration bitfield: - 1'b0: disabled - 1'b1: enabled |
| RESET        | 1      | 1     | W           | 0x0         | Timer high counter reset command bitfield. Cleared after Timer high reset execution. |
| IRQEN        | 2      | 1     | R/W         | 0x0         | Timer high compare match interrupt enable configuration bitfield: - 1'b0: disabled - 1'b1: enabled |
| IEM          | 3      | 1     | R/W         | 0x0         | Timer high input event mask configuration bitfield: - 1'b0: disabled - 1'b1: enabled |
| MODE         | 4      | 1     | R/W         | 0x0         | Timer high continuous mode configuration bitfield: - 1'b0: Continue mode - continue incrementing Timer high counter when compare match with CMP_LO occurs. - 1'b1: Cycle mode - reset Timer high counter when compare match with CMP_LO occurs. |
| ONE_S        | 5      | 1     | R/W         | 0x0         | Timer high one shot configuration bitfield: - 1'b0: let Timer high enabled counting when compare match with CMP_LO occurs. - 1'b1: disable Timer high when compare match with CMP_LO occurs. |
| PEN          | 6      | 1     | R/W         | 0x0         | Timer high prescaler enable configuration bitfield: - 1'b0: disabled - 1'b1: enabled |
| CLKCFG       | 7      | 1     | R/W         | 0x0         | Timer high clock source configuration bitfield: - 1'b0: FLL or FLL+Prescaler - 1'b1: Reference clock at 32kHz |

### CNT_LO

#### Fields

| Field Name   | Offset | Width | Access Type | Reset Value | Description |
| ---          | ---    | ---   | ---         | ---         | ---         |
| CNT_LO       | 0      | 32    | R/W         | 0x0         | Timer Low counter value bitfield. |


### CNT_HI

#### Fields

| Field Name   | Offset | Width | Access Type | Reset Value | Description |
| ---          | ---    | ---   | ---         | ---         | ---         |
| CNT_HI       | 0      | 32    | R/W         | 0x0         | Timer High counter value bitfield. |

### CMP_LO

#### Fields

| Field Name   | Offset | Width | Access Type | Reset Value | Description |
| ---          | ---    | ---   | ---         | ---         | ---         |
| CMP_LO       | 0      | 32    | R/W         | 0x0         | Timer Low comparator value bitfield. |

### CMP_HI

#### Fields

| Field Name   | Offset | Width | Access Type | Reset Value | Description |
| ---          | ---    | ---   | ---         | ---         | ---         |
| CMP_HI       | 0      | 32    | R/W         | 0x0         | Timer High comparator value bitfield. |

### START_LO

#### Fields

| Field Name   | Offset | Width | Access Type | Reset Value | Description |
| ---          | ---    | ---   | ---         | ---         | ---         |
| STRT_LO      | 0      | 1     | W           | 0x0         | Timer Low start command bitfield. When executed, CFG_LO.ENABLE is set. |

### START_HI

#### Fields

| Field Name   | Offset | Width | Access Type | Reset Value | Description |
| ---          | ---    | ---   | ---         | ---         | ---         |
| STRT_HI      | 0      | 1     | W           | 0x0         | Timer High start command bitfield. When executed, CFG_HI.ENABLE is set. |

### RESET_LO

#### Fields

| Field Name   | Offset | Width | Access Type | Reset Value | Description |
| ---          | ---    | ---   | ---         | ---         | ---         |
| RST_LO       | 0      | 1     | W           | 0x0         | Timer Low counter reset command bitfield. When executed, CFG_LO.RESET is set. |

### RESET_HI

#### Fields

| Field Name   | Offset | Width | Access Type | Reset Value | Description |
| ---          | ---    | ---   | ---         | ---         | ---         |
| RST_HI       | 0      | 1     | W           | 0x0         | Timer High counter reset command bitfield. When executed, CFG_HI.RESET is set. |
