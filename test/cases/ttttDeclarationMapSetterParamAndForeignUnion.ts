// @declaration: true
// @declarationMap: true
// @strict: true
// Maps: an interface's setter parameter is mapped (pi's agent types), and a node reused from another file whose
// first member is a keyword (`string | null`) isn't mapped into this one (pi's bug-report)
// @filename: acc.ts
export interface A {
	set tools(tools: string[]);
	get tools(): string[];
	get x(): number;
	set x(v: number);
}
export type T = {
	set y(v: number);
};
// @filename: b.ts
export interface R {
    s: string | null;
    n: number;
}
// @filename: a.ts
import type { R } from "./b";
export const f = (r: R) => ({ ...r });
