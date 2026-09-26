// @strict: true
type B = { s: string; 0: number; 1?: number; };
declare const k: keyof B;
declare const o: Omit<B, 0>;
declare const e: Exclude<keyof B, 0>;
const r: {s: string} = o;
declare const x: Pick<B, "s" | 1>;
