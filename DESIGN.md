# Design

<!-- impeccable:design-schema 1 -->

## World

**Service manual, not storefront.** The site is a field document for hardware, set
dark by default (a bench screen at night, a laptop next to the machine): ink ground
with a drafting dot grid, light typography, hairline rules instead of boxes.
Data (board, bus, version) is set in monospace because it *is* measurement; prose is
never set in mono. Depth comes from rules and spacing, never from shadows.

## Colour

| Token | Value | Use |
|---|---|---|
| `--paper` | `#121614` | page ground |
| `--paper-2` | `#1b211d` | inline code, download block, ghost hover |
| `--ink` | `#eef1ec` | body, headings, primary button ground |
| `--ink-2` | `#b6c0ba` | secondary text, spec line |
| `--ink-3` | `#94a09a` | labels, captions (≥4.5:1 on ground) |
| `--rule` | `#2d3631` | section rules |
| `--accent` | `#f0813f` | links, list numbers, hover |
| `--ok` | `#62c08d` | "released" state |

One accent, one state colour. No gradients on text, no glass, no coloured halos.

## Type

- Sans: system UI stack, 17px/1.6 body, measure ≤ 65ch.
- Mono: system mono for data, labels in small caps tracking, never for prose.
- Headings: 680 weight, letter-spacing −0.02…−0.035em, `text-wrap: balance`.
- Section headings are uppercase mono at 1.02rem — they are headings, not eyebrows.

## Layout

- Sheet: max 1180px, 28px gutters, masthead rule 2px ink.
- Index: 292px sticky rail (how to choose + family facts) beside a ruled list of packs.
- Pack page: prose column + 320px aside (download block, spec table).
- Packs are ruled rows, never cards; no nested containers.
- Breakpoints: 940px stacks the columns (aside first on pack pages), 620px stacks
  the row actions.

## Motion

One authored entrance: rows settle 6px over 0.5s with a 50ms stagger, disabled under
`prefers-reduced-motion`. Buttons transition background over 0.18s. Nothing else moves.

## States

Hover (accent underline or accent fill), focus-visible 2px accent outline offset 3px,
selection accent/paper, disabled and empty states exist in copy rather than as spinners.

## Voice

State the job and the constraint. No superlatives, no "powerful", no exclamation marks.
Errors name the problem and the recovery.

## Assets

`site/assets/img/` — one screen per pack, captured from the real UI; animated GIF where
the thing itself moves (camera stream). English UI, no personal data in frame.
