# Chat Transcript

**Date:** September 20, 2026

## User
Explain how the power manager module controls the number of phases used for charging an EV. Check if it controls the phases itself and, if not, which module does. Specifically, check and explain the interaction between the `power_manager` and `em_phase_switcher` modules. Keep the answer short and concise.

## Assistant
`power_manager` does not switch phases itself. It exposes the phase-switching API, validates requests, tracks switching state, and delegates through the `PhaseSwitcherBackend` interface.

`em_phase_switcher` registers itself as the backend with `power_manager`. When a phase change is requested, `PowerManager::switch_phases()` delegates to the backend. `em_phase_switcher` then coordinates stopping the charger, control-pilot disconnection, contactor switching, and reconnection. `em_v1` controls the physical contactor.

Responsibility split:

- Power Manager: policy/API and delegation
- EM Phase Switcher: phase-switching state machine
- EM V1: physical contactor control

## User
Where in the code is the decision done regarding how many phases are to be used? Is this also in the power manager module or some other module?

## Assistant
The automatic decision is made in the **Charge Manager**, not the Power Manager:

1. `power_manager.cpp:update_energy()` calculates available power/current and writes per-phase limits to `cm_limits`.
2. `charge_manager.cpp:run_allocator()` calls `allocate_current(...)`.
3. `current_allocator.cpp` selects 3 phases first and falls back to 1 phase if the limits do not allow 3 phases. It stores the result as `phase_alloc = 3` or `phase_alloc = 1` and sends it as `allocated_phases`.

The phase switcher then applies that decision.

## User
How does `em_phase_switcher.cpp::filter_command_packet()` receive the change?

## Assistant
The flow is:

```text
Charge Manager allocator
  → charger_allocation_state.allocated_phases
  → ChargeManager::send_client_packet()
  → CMNetworking::send_manager_update()
  → cm_command_packet.v2.allocated_phases
  → EMPhaseSwitcher::filter_command_packet()
  → contactor switching
```

`send_manager_update()` writes the phase count into `command_pkt.v2.allocated_phases`. Before the packet is sent, `CMNetworking::send_command_packet()` calls:

```cpp
em_phase_switcher.filter_command_packet(client_id, command_pkt);
```

The phase switcher reads the field, compares it with `em_v1.get_phases()`, and starts its state machine if they differ.

## User
EM phase switcher is a dedicated device. WARP3 and 4 chargers support internal phase switching. How is this controlled? Explain modules involved and their interaction.

## Assistant
For WARP3/4 internal phase switching, `em_phase_switcher` is not involved. The backend is the EVSE module, typically `EVSEV2`.

```text
Power Manager
  → calculates per-phase current limits
  → Charge Manager allocator selects 1 or 3 phases
  → Charge Manager sends allocated_phases in the CM command packet
  → EVSE receives the packet
  → EvseCommon calls EVSEV2::switch_phases()
  → EVSE Bricklet performs internal phase switching
```

Relevant modules:

- `current_allocator.cpp`: selects `phase_alloc = 3` or `phase_alloc = 1`.
- `charge_manager.cpp`: sends `charger_alloc.allocated_phases`.
- `evse_common.cpp`: receives `phases_requested` and calls `backend->switch_phases(phases_wanted)`.
- `evse_v2.cpp`: calls `tf_evse_v2_set_phase_control(&device, phases_wanted)`.

`EVSEV2` has backend priority 12, while `EMPhaseSwitcher` has priority 8, so `EVSEV2` is selected on WARP3/4 when available.

## User
So the same interface (via network packet) is used as when using the external energy manager? Even though the EVSE module runs inside the same device as the power manager, correct?

## Assistant
Correct. The same Charge Manager command protocol is used in both cases, with the phase count in `cm_command_packet.v2.allocated_phases`.

For an integrated WARP3/4 EVSE, the command goes through the local EVSE Charge Manager client path rather than a direct `PowerManager → EVSEV2` call. `evse_common` receives it and calls `EVSEV2::switch_phases()`.

For an external Energy Manager, `EMPhaseSwitcher` filters the outgoing command and switches the external contactor. The protocol is shared, but the final backend differs.

## User
Explain the interaction between EVSE/EM module and the Charge Manager. When charging starts, how do EVSE/EM determine how many phases are to be used? When phase switching is initiated, has charging already been stopped or is this handled by EVSE/EM? The EVSE and EM cases may differ. Keep the answer concise.

## Assistant
The Charge Manager decides the requested phase count; EVSE/EM apply it.

### Integrated WARP3/4 EVSE

1. `current_allocator.cpp` selects 1 or 3 phases based on the available per-phase limits.
2. `ChargeManager` sends this as `cm_command_packet.v2.allocated_phases`.
3. `evse_common.cpp` receives it and calls `backend->switch_phases(phases_wanted)`.
4. `EVSEV2` calls `tf_evse_v2_set_phase_control()`.

The EVSE path does not explicitly stop charging in `evse_common.cpp` before calling the backend. It checks whether switching is allowed; the EVSE/EVSE Bricklet handles the internal transition.

### External Energy Manager (`em_phase_switcher`)

`EMPhaseSwitcher::filter_command_packet()` explicitly handles the transition:

1. Detects a requested phase count different from the current count.
2. Sets `allocated_current = 0` in the outgoing command.
3. Waits until the charger reports zero allowed current.
4. Requests and waits for control-pilot disconnection.
5. Switches the EM contactor.
6. Waits for the switch and then permits charging/CP reconnection.

Therefore, the EVSE and EM paths differ: the EM module contains an explicit stop → CP disconnect → contactor switch → reconnect state machine, while the EVSE backend receives the phase command and handles the internal transition.

## User
Export the contents of this chat again in an md file. Commit the file to the `feature-use_charge_manager` branch, not `master`.

## Assistant
A Markdown transcript was requested for the `feature-use_charge_manager` branch.

## User
Again export the chat as md into the current branch (`feature-use_charge_manager`).

## Assistant
This file is the requested Markdown export for the current branch.
