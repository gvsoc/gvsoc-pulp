# Snitch SSR

## Registers

| Register Name | Offset | Size | Register Type | Host Access Type | Block Access Type | Default | Description                                           |
| ------------- | ------ | ---- | ------------- | ---------------- | ----------------- | ------- | ----------------------------------------------------- |
| STATUS        | 0x00   | 32   | Config        | R                | RW                | 0x00    |                      |
| REPEAT        | 0x04   | 32   | Config        | R                | RW                | 0x00    |                      |
| BOUNDS_0      | 0x08   | 32   | Config        | R                | RW                | 0x00    |                      |
| BOUNDS_1      | 0x0C   | 32   | Config        | R                | RW                | 0x00    |                      |
| BOUNDS_2      | 0x10   | 32   | Config        | R                | RW                | 0x00    |                      |
| BOUNDS_3      | 0x14   | 32   | Config        | R                | RW                | 0x00    |                      |
| STRIDES_0     | 0x18   | 32   | Config        | R                | RW                | 0x00    |                      |
| STRIDES_1     | 0x1C   | 32   | Config        | R                | RW                | 0x00    |                      |
| STRIDES_2     | 0x20   | 32   | Config        | R                | RW                | 0x00    |                      |
| STRIDES_3     | 0x24   | 32   | Config        | R                | RW                | 0x00    |                      |
| RPTR_0        | 0x60   | 32   | Config        | R                | RW                | 0x00    |                      |
| RPTR_1        | 0x64   | 32   | Config        | R                | RW                | 0x00    |                      |
| RPTR_2        | 0x68   | 32   | Config        | R                | RW                | 0x00    |                      |
| RPTR_3        | 0x6C   | 32   | Config        | R                | RW                | 0x00    |                      |
| WPTR_0        | 0x70   | 32   | Config        | R                | RW                | 0x00    |                      |
| WPTR_1        | 0x74   | 32   | Config        | R                | RW                | 0x00    |                      |
| WPTR_2        | 0x78   | 32   | Config        | R                | RW                | 0x00    |                      |
| WPTR_3        | 0x7C   | 32   | Config        | R                | RW                | 0x00    |                      |
