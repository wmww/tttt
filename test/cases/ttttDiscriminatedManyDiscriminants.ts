// @strict: true
// An object whose discriminant is a union relates to a discriminated union by combinations (tsc's
// typeRelatedToDiscriminatedType), however many of its properties the target discriminates by: here ten null in one
// member besides `state` (tsc's findDiscriminantProperties has no cap; only the combinations, 25, have one). openclaw's
// placement-projector.test.ts `project({ ...placement, state })` with `state: "draining" | "reconciling" | "reclaimed"`
type Unset = { a: null; b: null; c: null; d: null; e: null; f: null; g: null; h: null; i: null; j: null };
type Filled = { a: string; b: number; c: string; d: string; e: string; f: number; g: string; h: number; i: string; j: number | null };
type Rec =
    | (Unset & { state: "local" })
    | (Filled & { state: "active" })
    | (Filled & { state: "draining" })
    | (Filled & { state: "reconciling" });
declare function use(r: Rec): void;
declare const set: Filled;
declare const state: "draining" | "reconciling";
use({ ...set, state });
const ok: Rec = { ...set, state };
const bad: Rec = { ...set, state: state as "draining" | "local" };
