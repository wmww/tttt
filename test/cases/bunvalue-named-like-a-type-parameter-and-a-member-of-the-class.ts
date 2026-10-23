// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "target": "es2022", "module": "esnext", "moduleResolution": "bundler", "types": [], "lib": ["es2022"] }, "files": ["a.ts"] }
// @filename: a.ts
import { Decode, Plain } from "./b";
export class V1<Decode extends unknown = number> { public Decode(value: unknown): Decode { const r = Decode(1, 2, 3); return r as never; } }
export class V2<T = number> { public Decode(value: unknown): T { const r = Decode(1, 2, 3); return r as never; } }
export class V3<Decode = number> { public other(value: unknown): Decode { const r = Decode(1, 2, 3); return r as never; } }
export class V4<Plain = number> { public Plain(value: unknown): Plain { const r = Plain(1, 2, 3); return r as never; } }
export class V5<Decode = number> { hasCodec = true; Parse(v: unknown): Decode { return v as never; } public Decode(value: unknown): Decode { const result = this.hasCodec ? Decode(1, 2, 3) : this.Parse(value); return result as never; } }
export function f6<Decode>(x: Decode) { return Decode(1, 2, 3); }
export const sentinel: number = "";
export class W1<Decode = number> { Decode(v: unknown): Decode { return v as never; } field = Decode(1, 2, 3); }
export class W2<Decode = number> { Decode(v: unknown): Decode { return v as never; } static s = Decode(1, 2, 3); }
export class W3<Decode = number> { Decode(v: unknown): Decode { const g = () => Decode(1, 2, 3); return g() as never; } }
export class W4<Decode = number> { Decode(v: unknown): Decode { type Q = typeof Decode; const q: Q = Decode; return q(1, 2, 3) as never; } }
export class W5<Decode = number> { Decode = 1; m(): Decode { return Decode(1, 2, 3) as never; } }
export class W6<Decode = number> { get Decode(): Decode { return Decode(1, 2, 3) as never; } }
export class W7<Decode = number> { Decode(v: unknown, d = Decode(1, 2, 3)): Decode { return d as never; } }
export class W8<Decode = number> { [Decode(1, 2)](): Decode { return 1 as never; } Decode(): void {} }
export const W9 = class Named<Decode = number> { Decode(v: unknown): Decode { Named; return Decode(1, 2, 3) as never; } };
export interface I1<Decode = number> { Decode(v: unknown): Decode; q: typeof Decode; }
export function g1<T>(T: number) { return T + 1; }
export function g2<Decode>(Decode: (a: number) => number, x: Decode) { return Decode(1); }
export class X1<Missing = number> { Missing(v: unknown): Missing { return Missing(1) as never; } }
// @filename: b.ts
export function Decode(a: number, b: number): number;
export function Decode(a: number, b: number, c: number): number;
export function Decode(...args: number[]): number { return args.length; }
export function Plain(a: number, b: number, c: number): number { return a + b + c; }
