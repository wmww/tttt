// @declaration: true
// A property several parts of an intersection have is a Property (tsc's createUnionOrIntersectionProperty), not a
// method, also as a spread copies it: printed with its intersected type
declare class D { render(...props: unknown[]): unknown; update(): void; }
export const f = (c: new () => D & { render(): unknown }) => class extends c {};
export declare const x: D & { render(): unknown };
export const y = { ...x };
type I = { m(): void } & { m(x: number): void; n(): void };
declare const i: I;
export const z = { ...i };
