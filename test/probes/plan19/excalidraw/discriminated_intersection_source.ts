// @strict: true
type Base = Readonly<{ id: string; x: number }>;
type A = Base & Readonly<{ type: "a" }>;
type B = Base & Readonly<{ type: "b" }>;
type G = A | B;
type Merge<M, N> = Omit<M, keyof N> & N;
declare const m: Merge<G, { type: "a" | "b" }>;
const g: G = m;
const bad: G & { isDeleted: boolean } = m;
declare const m2: { x: number } & { type: "a" | "b" };
const g2: { x: number; type: "a" } | { x: number; type: "b" } = m2;
const bad2: { x: string; type: "a" } | { x: number; type: "b" } = m2;
