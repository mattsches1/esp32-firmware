# Chat Transcript

**Date:** September 17, 2026

## 1. User
Explain how the power manager module controls the number of phases used for a charging an ev. Check if it controls the phases itself and if not, which module does. Specifically, check and explain the interaction between the power_mqnager and the em_phase_switcher modules.

Keep your answer short and concise.

## 1. Assistant
- `power_manager` does not switch phases itself. It exposes the phase-switching API, validates requests, tracks switching state, and delegates through the `PhaseSwitcherBackend` interface.
- During setup, `em_phase_switcher` registers itself as the power manager’s backend via `power_manager.register_phase_switcher_backend(this)`.
- When a phase change is requested, `power_manager::switch_phases()` calls `phase_switcher_backend->switch_phases(...)`.
- `em_phase_switcher` performs the switching coordination: it waits for the charger to stop, disconnects the control pilot if necessary, and calls `em_v1.set_contactor_for_em_phase_switcher(...)`.
- The EM contactor determines the actual count: off = 1 phase, on = 3 phases.

Responsibility split:

- Power Manager: policy/API/coordinator
- EM Phase Switcher: switching state machine
- EM V1: physical contactor control

## 2. User
Where in the code is the decision done regarding how many phases are to be used? Is this also in the power manager module or some other module?

## 2. Assistant
The automatic decision is made in the Charge Manager, not the Power Manager:

1. `power_manager.cpp:update_energy()` calculates available power/current and writes per-phase limits to `cm_limits`.
2. `charge_manager.cpp:run_allocator()` calls `allocate_current(...)`.
3. `current_allocator.cpp` chooses the phase allocation. It tries 3 phases first and, if that exceeds the limits, tries 1 phase. The result is stored as `phase_alloc = 3` or `phase_alloc = 1` and sent as `allocated_phases`.

Then `em_phase_switcher.cpp::filter_command_packet()` receives the allocation, compares it with the current phase count, and performs the physical switching through the EM contactor.

Summary:

- Power Manager: calculates current/power limits.
- Charge Manager: decides whether to use 1 or 3 phases.
- EM Phase Switcher: applies the decision by switching the contactor.

## 3. User
How does em_phase_switcher.cpp::filter_command_packet() receive the change?

## 3. Assistant
`filter_command_packet()` is called synchronously while the Charge Manager sends its command packet:

1. `ChargeManager::send_client_packet()` reads `charger_alloc.allocated_phases` and passes it to `cm_networking.send_manager_update(...)`.
2. `CMNetworking::send_manager_update()` stores it in `command_pkt.v2.allocated_phases`.
3. `CMNetworking::send_command_packet()` calls:
   ```cpp
   em_phase_switcher.filter_command_packet(client_id, command_pkt);
   ```
4. `filter_command_packet()` reads `command_packet->v2.allocated_phases`, compares it with `em_v1.get_phases()`, and starts its switching state machine if they differ.

Flow:

```text
Charge Manager allocator
  → charger_allocation_state.allocated_phases
  → send_client_packet()
  → CMNetworking::send_manager_update()
  → cm_command_packet.v2.allocated_phases
  → em_phase_switcher.filter_command_packet()
  → contactor switching
```

The packet is passed as a pointer in memory before it is transmitted over UDP.

## 4. User
Em phase switcher is a dedicated device. Warp3 and 4 chargers support internal phase switching. How is this controlled? Explain modules involved and their interaction.

## 4. Assistant
For WARP3/4 internal phase switching, `em_phase_switcher` is not involved. The backend is the EVSE module itself, typically `EVSEV2`.

Automatic flow:

```text
Power Manager
  → calculates per-phase current limits
  → Charge Manager allocator selects 1 or 3 phases
  → Charge Manager sends allocated_phases in the CM command packet
  → EVSE receives the packet
  → EvseCommon calls EVSEV2::switch_phases()
  → EVSE Bricklet performs the internal phase switching
```

Key modules:

- `current_allocator.cpp`: chooses `phase_alloc = 3` or `phase_alloc = 1`.
- `charge_manager.cpp`: sends `charger_alloc.allocated_phases`.
- `evse_common.cpp`: receives `phases_requested` and calls `backend->switch_phases(phases_wanted)`.
- `evse_v2.cpp`: calls `tf_evse_v2_set_phase_control(&device, phases_wanted)`.

For manual/external requests, `PowerManager::switch_phases()` delegates to the registered `PhaseSwitcherBackend`. Backend priority is `EVSEV2 = 12` and `EMPhaseSwitcher = 8`, so `EVSEV2` is selected on WARP3/4 when available.

## 5. User
So the same interface (via network packet) is used as when using the external energy manager? Even though the evse module runs inside the same device as the power manager, correct?

## 5. Assistant
Correct, with one distinction:

- The same Charge Manager command protocol is used in both cases; the phase count is carried in `cm_command_packet.v2.allocated_phases`.
- For an integrated WARP3/4 EVSE, the command goes through the local EVSE CM client path, rather than a direct `PowerManager → EVSEV2` call.
- `evse_common` receives it and calls `backend->switch_phases(phases_wanted)`, with `EVSEV2` as the backend.

External Energy Manager:

```text
Charge Manager → CM packet → EMPhaseSwitcher filters packet
                         → switches external contactor
```

Integrated WARP3/4:

```text
Charge Manager → CM packet → local EVSE CM client
                         → EVSEV2::switch_phases()
                         → internal EVSE phase switching
```

The protocol is shared, but the final phase-switching backend differs.

## 6. User
Explain the interaction between evse/em module and the charge manager. When the charging process starts, how do evse/em determine how many phases are to be used? And when phase switching is initiated, has the charging process already been stopped or is this handled by evse/em? It may be that these two cases (evse/em) differ in this regard. If so, provide information for both. Keep your answer concise.

## 6. Assistant
The Charge Manager decides the requested phase count; EVSE/EM apply it.

### Integrated WARP3/4 EVSE

1. `current_allocator.cpp` selects 1 or 3 phases based on available per-phase limits.
2. `ChargeManager` sends this as `cm_command_packet.v2.allocated_phases`.
3. `evse_common.cpp` receives the packet and calls:
   ```cpp
   backend->switch_phases(phases_wanted);
   ```
4. The `EVSEV2` backend calls `tf_evse_v2_set_phase_control()`.

The EVSE module does not explicitly stop charging first in this path. It checks whether switching is currently allowed; the EVSE/EVSE Bricklet performs the required internal transition.

### External Energy Manager (`em_phase_switcher`)

`EMPhaseSwitcher::filter_command_packet()` explicitly handles stopping:

1. Detects that the requested phases differ from the current phases.
2. Sets `allocated_current = 0` in the outgoing command.
3. Waits until the charger reports zero allowed current.
4. Requests control-pilot disconnection and waits for confirmation.
5. Switches the EM contactor.
6. Waits for the switch, then permits charging and CP reconnection again.

Therefore:

- EVSE: the phase-switch command is passed to the EVSE backend, which handles the transition internally.
- EM: `em_phase_switcher` implements an explicit stop → CP disconnect → contactor switch → reconnect state machine.
