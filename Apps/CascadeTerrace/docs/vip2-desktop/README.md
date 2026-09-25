# VI-P2 desktop reference evidence

Generate these screenshots and run the frontend integration assertions from the app directory:

```sh
./build/cascade --platform-selftest /tmp/anaphorum-desktop-evidence
```

The SDL dummy video driver exercises the actual frontend and rendering path. Images show the complete window, including letterboxing, at 320×440, 640×320, 480×480, and 960×640. The fixed 160×240 render surface fits the available content rectangle without stretching; touch uses the exact inverse transform. The desktop content rectangle is the SDL client area. These are host screenshots, not physical P4 captures.

The self-test checks viewport boundaries, menu entry and Controls, successful capture, conflict cancellation, capacity/invalid rejection, a 10ms press/release processed after a 150ms render-like stall, queued analog menu taps, focus loss/regrant with stale held input, per-finger outside-viewport release, and Quit. Existing causal-before/causal-after script fixtures also pass through this frontend.

SDL input events retain their timestamps. They feed the shared action model before simulation; its accumulated edges and weighted axes preserve events that occur between rendered frames. This is event ingestion on the SDL main thread, not a claim of an independent 10ms desktop hardware sampler. The native Tactility frontend owns the device sampling task.

Controller bindings identify backend, control, GUID plus serial (or device path when serial is absent), and axis direction. Runtime connection IDs only identify held sources and disconnects. A controller without a serial may require rebinding after its system device path changes. Start cancels binding capture; digital buttons and analog directions can otherwise be captured. D-pad or left stick navigates menus; A selects, B goes back, X pages current bindings, and Y restores defaults. Keyboard navigation uses arrows, Enter, Esc, PgDn, and R. Touch selects visible rows and buttons. Binding conflicts require explicit replacement; gameplay controls are reconciled at menu and focus transitions.

The desktop has no eleven-button debug toolbar. Debug operations are available in the qualification script path and optional `--qualify` shortcuts. Production interaction resolves the current nearby entity and preserves its identity throughout the conversation. Camera yaw retains the existing saved view angle while pitch remains transient; fractional yaw is preserved and one-shot jump edges wait for the next 20ms physics step.
