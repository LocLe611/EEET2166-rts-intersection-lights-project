# EEET2166 Distributed Traffic Control System

Five QNX nodes are implemented:

- `inter1` -> I1 local intersection controller (`intersection 1`)
- `inter2` -> I2 local intersection controller (`intersection 2`)
- `cross` -> X1 railway crossing controller (`crossing`)
- `central` -> supervisory controller (`central`)
- `display` -> monitoring-only node (`monitor`)

All nodes use Qnet/GNS global names. The services are `i1_ctrl`, `i2_ctrl`, `x1_ctrl`, `central_ctrl`, and `display_ctrl`.

## Build

Clean and build all four Momentics projects after changing anything in `common/`:

```text
central
intersection
crossing
monitor
```

The same `intersection` executable is used for I1 and I2 with different arguments:

```text
I1: intersection 1
I2: intersection 2
```

Use `-s` only for standalone intersection testing without the X1 heartbeat requirement.

## Startup

A convenient demo order is:

```text
central -> inter1 -> inter2 -> cross -> display
```

The design does not depend on this order. Nodes retry their communication links and reconnect after another node restarts.

## Commands

### I1 / I2

```text
p  pedestrian request
v  side-road vehicle demand
e  inject conflicting-green fault
h  show commands
q  quit
```

Mode changes, overrides, railway events and fault clearing are intentionally handled by Central/X1 rather than duplicated as local keyboard commands.

### X1

```text
A  train approaching
D  gate-down sensor confirmed
C  train cleared crossing
F  inject gate/sensor fault
R  clear latched fault
H  show commands
Q  quit
```

After `A`, X1 automatically commands the gate to lower and starts the gate-confirmation timer. If `D` is not received within `X1_GATE_TIMEOUT_MS`, X1 enters the fail-safe fault state.

### Central

```text
1  set FIXED / SENSOR mode
2  set / clear override
3  clear controller fault
4  toggle automatic schedule
5  show system status
+  advance simulated time one hour
h  show commands
q  quit
```

### Monitor

```text
Q  quit
```

The monitor receives `MSG_SYSTEM_STATUS` from Central and has no control commands.

## Communication paths

```text
X1 -> I1/I2        railway pulses and heartbeat
I1/I2 -> Central   intersection/light status
Central -> I1/I2   mode, override and fault-clear commands
X1 -> Central      railway/fault status
Central -> Display aggregated system status
```

I1 and I2 keep local control if Central or Display is unavailable. Loss of X1 heartbeat is fail-safe: R3 is held red until valid railway state returns.
