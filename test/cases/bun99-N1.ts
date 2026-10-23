// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2023"], "module": "esnext", "moduleResolution": "bundler" } }
// @filename: a.ts
type One = { a: boolean; b: string | undefined };
type Left = { kind: "left"; l: number; m(): string };
type Right = { kind: "right"; r: string; m(): number };
declare function use<T>(select: () => T): { data: NoInfer<T> | undefined };
declare function one(): One | undefined;
declare function either(): Left | Right | undefined;
declare function memo<T>(factory: () => T): T;
declare function id<T>(value: T): T;
declare function pair<T, V>(value: T, other: V): [T, V];
const { data: x } = use(one);
const { data: u } = use(either);
const whole = use(one);
declare const declared: NoInfer<One | undefined> | null;

export function returned(s: string): { s: string; a: boolean; b: string | undefined } | undefined {
  return memo(() => (x !== undefined && x !== null ? { s, ...x } : undefined));
}
export const v01: never = memo(() => (x !== undefined ? { s: 1, ...x } : undefined));
export const v02: never = memo(() => (x ? { ...x, s: 1 } : undefined));
export const v03: never = id(x !== undefined ? { s: 1, ...x } : undefined);
export const v04: never = pair(x ? { s: 1, ...x } : undefined, 1);
export const v05: never = memo(() => (whole.data !== undefined ? { s: 1, ...whole.data } : undefined));
export const v06: never = memo(() => (declared != null ? { s: 1, ...declared } : undefined));
export const v07: never = memo(function () { return x !== undefined ? { s: 1, ...x } : undefined; });
export const v08: never = memo(() => { if (x !== undefined) return { s: 1, ...x }; return undefined; });
export const v09: never = memo(() => (u !== undefined ? { s: 1, ...u } : undefined));
export const v10: never = id(u && "l" in u ? { s: 1, ...u } : undefined);
export const v11: never = id(u?.kind === "right" ? { ...u, s: 1 } : undefined);
export const v12: never = memo(() => ({ s: 1, ...x }));
export const v13: never = (() => (x !== undefined ? { s: 1, ...x } : undefined))();
