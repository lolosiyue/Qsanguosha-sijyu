# Controller response invariants

This branch is based on `bf3671548800167bec4056e1da5842de41bec864`.
The following are changes made on this branch, not claims about the unchanged base.

- `ClientCore::validateAgainst()` rejects explicit `InteractionResponseKind::Cancel`
  whenever the active request is noncancelable, even when its minimum is zero.
- A legitimate zero-selection structured answer continues through its native
  response-shape validator. Gongxin may acknowledge inspection with zero cards;
  this must remain distinct from declining a mandatory prompt.
- The generic protocol builder gives mandatory `CHOOSE_CARD` and `RESPONSE_CARD`
  requests a nonzero minimum, matching the desktop ingress. Optional replies
  continue to use the existing cancellation/empty-answer encoding.
- A `QML_INTERACT` request declaring `parameters.controller_ui` is checked by the
  versioned finite contract validator. Missing fields, additional fields, wrong
  QVariant types, bounds violations and duplicate selected values are rejected.
  An ordered selection preserves the supplied order. A declared cancel value is
  accepted only by exact comparison and the descriptor's `can_cancel` policy.
- UI closure, a superseded request and a disconnected device do not manufacture
  a server answer. A modal adapter retires when its request/generation changes.

`tools/autotest/controller_contract_probe.cpp` is a standalone QtCore probe;
18 assertions passed against the actual core static library on Qt 6.8.2.
It is neither a GUI test nor evidence of real controller hardware.
No CMake test target is introduced. See `controller-integration.md` for the
virtual-device and live GUI verification boundaries.
