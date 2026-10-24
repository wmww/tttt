// @strict: true
// @noEmit: true
// tsc computes a reference's flow type once, as it checks it: `if (r)` here leaves the else branch's loop junction
// incomplete (its way in meets the first loop's shared node as its back edge left it), so the narrowed `r` inside is
// the silent never and `r.previous` no error. tttt's check pass asked the flow again for the unassigned-variable issue,
// which cached that junction complete and turned the later reference into a plain never (TS2339). paperclip's
// tool-access.ts
type M = { previous: number };
declare const xs: string[];
export function f1(c: boolean) {
  let r: M | null = null;
  let app: number | null = null;
  for (const x of xs) {
  }
  if (c) {
  } else {
    for (let attempt = 0; attempt < 10 && !app; attempt += 1) {
      app = 1;
    }
  }
  if (r) {
    r.previous;
  }
}
