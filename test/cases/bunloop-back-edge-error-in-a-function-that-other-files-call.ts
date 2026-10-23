// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "target": "esnext", "module": "esnext", "moduleResolution": "bundler", "types": [] }, "files": ["b.ts", "c.ts", "a.ts"] }
// @filename: a.ts
interface P { k: "p"; next: P | Q | null } interface Q { k: "q"; next: P | Q | null }
declare const more: boolean; declare const p: P; declare const q: Q;
export function build() {
  let last: P | Q | null = null;
  while (more) {
    if (last === null) last = more ? p : q;
    else last = last.next = more ? q : p;
  }
  return last;
}
// @filename: b.ts
import { build } from "./a";
export const x = build();
// @filename: c.ts
import { build } from "./a";
export const y = [build()];
