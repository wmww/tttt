// A declaration file is an export context, which exports an ambient declaration only without its own `declare`
// (tsc's getEffectiveDeclarationFlags): `declare function f()` beside `export function f(x)` is TS2383
// (missing_checks.md)
// @filename: a.d.ts
declare function f(): void;
export function f(x: number): void;
declare function g(): void;
declare function g(x: number): void;
export function g(x: string): void;
function h(): void;
export function h(x: number): void;
export declare function k(): void;
declare function k(x: number): void;
// @filename: b.d.ts
declare function f(): void;
declare function f(x: number): void;
