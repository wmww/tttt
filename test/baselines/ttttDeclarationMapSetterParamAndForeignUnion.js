//// [test/cases/ttttDeclarationMapSetterParamAndForeignUnion.ts] ////

//// [acc.ts]
export interface A {
	set tools(tools: string[]);
	get tools(): string[];
	get x(): number;
	set x(v: number);
}
export type T = {
	set y(v: number);
};
//// [b.ts]
export interface R {
    s: string | null;
    n: number;
}
//// [a.ts]
import type { R } from "./b";
export const f = (r: R) => ({ ...r });


//// [acc.js]
export {};
//// [b.js]
export {};
//// [a.js]
export const f = (r) => ({ ...r });


//// [acc.d.ts]
export interface A {
    set tools(tools: string[]);
    get tools(): string[];
    get x(): number;
    set x(v: number);
}
export type T = {
    set y(v: number);
};
//# sourceMappingURL=acc.d.ts.map//// [b.d.ts]
export interface R {
    s: string | null;
    n: number;
}
//# sourceMappingURL=b.d.ts.map//// [a.d.ts]
import type { R } from "./b";
export declare const f: (r: R) => {
    s: string | null;
    n: number;
};
//# sourceMappingURL=a.d.ts.map