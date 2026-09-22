// @strict: true
interface O { target?: number; module?: string; other: boolean }
declare function f(o: Pick<O, "target" | "module">): void;
declare const o: O;
f(o);
type P = Pick<O, "target">;
interface PC { readonly path: string; kind: 1 }
declare function g(p: Readonly<PC>): void;
declare const pc: PC;
g(pc);
type Path = string & { __pathBrand: any };
type PPC = Path[] & { __brand: any };
declare function h(p: Readonly<PPC>): void;
declare const ppc: PPC;
h(ppc);
type RP = Readonly<PPC>;
declare const rp: RP;
const len: number = rp.length;
