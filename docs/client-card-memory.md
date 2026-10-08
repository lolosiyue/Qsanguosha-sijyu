# Client card memory

The desktop client distinguishes **certain hand cards** from **possible memories**.
Only IDs already delivered to this recipient enter either set. A concealed loss
moves remembered nonpublic cards to the possible set; public cards remain certain.
An identified loss removes the ID from both sets, and an empty hand clears both.
Seeing the ID enter another zone removes stale membership in other players' memories.
Seeing it enter this hand promotes it to certain. Complete observations and whole-hand
swaps preserve the distinction. Possible memories are never added to `hand_ids`,
passed to the AI, or counted as actual cards.

The existing hand viewer shows the real unknown-card backs plus separate, dimmed
`Possible ?` candidates, including the previously observed suit and number. Its
summary tooltip explicitly says candidates may have left the hand. Memory changes refresh
an open viewer, including observations in a different player's zone. The existing
`inovation_fengbi` restriction still hides this viewer. Long arrangements scale to
fit the existing clipped viewer area.

## Draw-pile edges and protocol limitations

Top and bottom memory are inward-ordered lists: the first top entry is next;
the first bottom entry is bottommost. A recipient's `$GuanxingTop` and
`$GuanxingBottom` logs are staged and committed only by their immediately following
`UPDATE_PILE`. Other intervening messages discard the staged data. The public
`#GuanxingResult` invalidates the previous order even for recipients who cannot see
the new card IDs. The existing pile summary displays valid edge memories.

All count-only updates otherwise invalidate order **even when the count is
unchanged**: `returnToTopDrawPile` and `returnToEndDrawPile` may reorder cards and
only publish a count. Unknown losses, insertions without an explicit position,
shuffle, state sync, replay seek, and viewpoint changes clear order memory.
Identified losses may remove IDs from the remembered edges before a subsequent
count update clears the remaining order conservatively.

This protocol canonicalizes `DrawPileBottom` to `DrawPile` in movement packets, and
`getNCards` can publish only a count before movements. Consequently the client does
not infer hidden draws from an edge, and does not infer a top/bottom insertion from
an ordinary movement packet. Rearrangements that suppress the private result logs
are not remembered. Broadening this requires explicit recipient-safe positional
notifications; server deck contents must not be used as a substitute.

`ClientPlayer::getKnownCards()` remains certain-only; `getUncertainCards()` is a
separate UI-facing projection. `Client::rememberedDrawPileTop()` and
`rememberedDrawPileBottom()` expose only committed edge memories. No server rules,
isolated AI strategy, or protocol schema were changed.

## Focused validation

Run from the repository root:

```sh
c++ -std=c++17 -Wall -Wextra -Werror -fsanitize=address,undefined \
  tools/autotest/card_memory_contract.cpp -o /tmp/card_memory_contract
/tmp/card_memory_contract
```

The contract covers public-card retention after hidden loss, certain-to-possible
downgrade, promotion, exclusion on later observation, batch-order independence,
empty-hand clearing, deduplication, staged edge confirmation, hidden-move reset,
count-preserving reorder reset, unrelated-message cancellation, invalid duplicate
or redacted edge observations, and shuffle/reset clearing. Qt integration must be
built with the project's Qt 6.11 SDK. The standalone tracker contract has no Qt
dependency and is not a substitute for GUI compilation or runtime validation.

## Design reference

The separation of known/uncertain sets and conservative deck-order invalidation
were independently implemented from the design in FreeKill-core
`ltk/core/known_card_tracker.lua` (`trackLoseCards`, `trackLoseFromDrawPile`,
`trackAddToDrawPile`), pinned at
`c19441690711b73ffb427b3e7974ec7e92e33bea` (GPLv3), with the companion FreeKill
reference pinned at `671b0ad698c36b703808d8b29f3483ce54b36631` (GPLv3).
Unlike the reference's full-deck reconciliation, this implementation never reads
an authoritative deck to recover hidden identities or positions.
