# Mailbox Model

A device model for mailbox-based inter-core or component communication. 
Each mailbox slot stores two 32-bit words `LETTER` and exposes send/receive control registers and interrupt outputs.

## Configuration

Create the mailbox with a slot count:

```python
from ips.pulp.mailbox.mailbox import Mailbox

mailbox = Mailbox(self, "mailbox", size=10)
```

The `size` property determines how many slots the model creates and how many pairs of send/receive IRQ ports it exposes.

## Memory map

Each mailbox slot occupies a `0x100` byte address window. For a mailbox mapped at base address `BASE`, slot `i` is addressed at:

```
BASE + (i * 0x100) + register_offset

0 <= i < size
```

| Register | Offset | Access in current implementation | Result |
|---|---:|---|---|
| Send status | `0x00` | Read | Return the pending send bit |
| Send set | `0x04` | Write | Set the pending send bit |
| Send clear | `0x08` | Write | Clear the pending send bit |
| Send enable | `0x0C` | Read/write | Read or change the send enable bit |
| Receive status | `0x40` | Read | Return the pending receive bit |
| Receive set | `0x44` | Write | Set the pending receive bit |
| Receive clear | `0x48` | Write | Clear the pending receive bit |
| Receive enable | `0x4C` | Read/write | Read or change the receive enable bit |
| Letter 0 | `0x80` | Read/write | Read or write payload word 0 |
| Letter 1 | `0x8C` | Read/write | Read or write payload word 1 |


Use aligned 32-bit MMIO reads and writes. Invalid offsets and unsupported access
sizes return `IO_REQ_INVALID`. Reads of write-only registers return zero and
writes to status registers have no effect.

## Interrupt interfaces

The Python configurator provides these connections for each slot:

- `o_SND_IRQ(id, interface)` binds `irq_snd_<id>`, the notification raised by a send.
- `o_RCV_IRQ(id, interface)` binds `irq_rcv_<id>`, the notification raised by a receive or acknowledgment.

This example maps the device and delivers slot 0’s send notification to the receiver through a PLIC input:

```python
mailbox = Mailbox(self, "mailbox", size=10)

ico.o_MAP(mailbox.i_INPUT(), "mailbox", base=0x20000000, size=10 * 0x100, rm_base=True)

mailbox.o_SND_IRQ(0, plic.i_IRQ(0))
```

Connect the output to the interface that should receive the interrupt. The slot index passed to `o_SND_IRQ` or `o_RCV_IRQ` must be less than `size`.

## Current control behavior

Each direction has its own pending and enable bits. A write to SET or CLR updates
the pending bit regardless of the written value. STAT reads report the pending
bit even when the IRQ is disabled.

The corresponding IRQ is high when both bits are set. Writing `0` to EN masks
the IRQ while preserving pending state. Writing `1` enables it and asserts the
line immediately if a notification is already pending. Other EN write values
return `IO_REQ_INVALID`. Reset clears the payload, pending and enable bits and
deasserts both IRQs.

## Build and test

The Python wrapper registers `ips/pulp/mailbox/mailbox.cpp` with `add_sources()`.