// @strict: true
// A reference narrowed to never while its loop is still at a partial type is the silent never at every flow node
// (tsc's newFlowType), not only at the condition: here the label after `if (...) fail()` joins the unreachable call
// with the narrowed branch, and `clip.axis` in the assignment the loop evaluates for its back edge reported TS2339 on
// never (deepmonkey-studio)
type ClipState = { readonly kind: "off" } | { readonly kind: "axis"; readonly axis: string };
declare function fail(): never;
declare const ops: number[];
export function frame(): ClipState {
  let clip: ClipState = { kind: "off" };
  for (const op of ops) {
    if (op === 1) {
      clip = { kind: "axis", axis: "a" };
    } else {
      if (clip.kind !== "axis") fail();
      clip = { kind: "axis", axis: clip.axis };
    }
  }
  return clip;
}
