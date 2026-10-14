// The checks of a merged interface's declarations together (TS2717, TS2300, TS2428) run once, by its first declaration
// in a checked file: under skipLibCheck that isn't a global augmentation in a .d.ts (react-scan's `interface Window` in
// extension/src/types/global.d.ts, whose unresolved `ReactRenderer` makes the property differ; vitest's `Matchers<R, T>`
// that msw's and qwik's tests augment as `Matchers<T = any>`), so the .ts file's declaration does it
// @strict: true
// @skipLibCheck: true
// @filename: g.d.ts
export {};
declare global {
  interface W { a: string; a: number; b: string; c(): void; hook?: { renderers: Map<number, ReactRenderer> } }
  interface M<R = void, T = unknown> {}
}
// @filename: t.ts
export {};
interface Ext { v: string }
declare global {
  interface W { b: number; c: string; d: 1; hook?: { renderers: Map<number, Ext> } }
  interface M<T = any> {}
}
