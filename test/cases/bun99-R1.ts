// @noTypesAndSymbols: true
// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2023"], "skipLibCheck": true } }
// @filename: a.ts
declare function wrap<T extends Function>(callback: T, deps: readonly unknown[]): T;
declare function wrapAny<T>(callback: T): T;
declare function wrapFn<T extends (...args: any[]) => any>(callback: T): T;
declare function use<T>(options: { items: readonly T[]; key?: (index: number) => string }): void;
declare function useRequired<T>(options: { items: readonly T[]; key: (index: number) => string }): void;
declare function usePlain(options: { items: readonly string[]; key?: (index: number) => string }): void;
declare function useArgs<T>(items: readonly T[], key?: (index: number) => string): void;
declare function useGuard<T>(options: { items: readonly T[]; is?: (value: unknown) => boolean }): void;
declare const items: readonly string[];
{ const key = wrap((index: number) => `k-${index}`, []); use({ items, key }); useRequired({ items, key }); usePlain({ items, key }); useArgs(items, key); const probe: never = key; }
{ const key = wrap((index: number) => { return `k-${index}`; }, []); use({ items, key }); useRequired({ items, key }); usePlain({ items, key }); useArgs(items, key); const probe: never = key; }
{ const key = wrap(function (index: number) { return `k-${index}`; }, []); use({ items, key }); useRequired({ items, key }); usePlain({ items, key }); useArgs(items, key); const probe: never = key; }
{ const key = wrap((index: number) => "k", []); use({ items, key }); useRequired({ items, key }); usePlain({ items, key }); useArgs(items, key); const probe: never = key; }
{ const key = wrap((index: number) => String(index), []); use({ items, key }); useRequired({ items, key }); usePlain({ items, key }); useArgs(items, key); const probe: never = key; }
{ const key = wrap(() => `k`, []); use({ items, key }); useRequired({ items, key }); usePlain({ items, key }); useArgs(items, key); const probe: never = key; }
{ const key = wrap((index: number, other?: number) => `k-${index}-${other}`, []); use({ items, key }); useRequired({ items, key }); usePlain({ items, key }); useArgs(items, key); const probe: never = key; }
{ const key = wrap((index: number) => (index ? `a-${index}` : `b-${index}`), []); use({ items, key }); useRequired({ items, key }); usePlain({ items, key }); useArgs(items, key); const probe: never = key; }
{ const key = wrap((index: number) => `k-${index}` as const, []); use({ items, key }); useRequired({ items, key }); usePlain({ items, key }); useArgs(items, key); const probe: never = key; }
{ const key = wrap((index: number) => `k-${`i-${index}`}`, []); use({ items, key }); useRequired({ items, key }); usePlain({ items, key }); useArgs(items, key); const probe: never = key; }
{ const key = wrapAny((index: number) => `k-${index}`); use({ items, key }); useRequired({ items, key }); usePlain({ items, key }); useArgs(items, key); const probe: never = key; }
{ const key = wrapAny((index: number) => { return `k-${index}`; }); use({ items, key }); useRequired({ items, key }); usePlain({ items, key }); useArgs(items, key); const probe: never = key; }
{ const key = wrapAny(function (index: number) { return `k-${index}`; }); use({ items, key }); useRequired({ items, key }); usePlain({ items, key }); useArgs(items, key); const probe: never = key; }
{ const key = wrapAny((index: number) => "k"); use({ items, key }); useRequired({ items, key }); usePlain({ items, key }); useArgs(items, key); const probe: never = key; }
{ const key = wrapAny((index: number) => String(index)); use({ items, key }); useRequired({ items, key }); usePlain({ items, key }); useArgs(items, key); const probe: never = key; }
{ const key = wrapAny(() => `k`); use({ items, key }); useRequired({ items, key }); usePlain({ items, key }); useArgs(items, key); const probe: never = key; }
{ const key = wrapAny((index: number, other?: number) => `k-${index}-${other}`); use({ items, key }); useRequired({ items, key }); usePlain({ items, key }); useArgs(items, key); const probe: never = key; }
{ const key = wrapAny((index: number) => (index ? `a-${index}` : `b-${index}`)); use({ items, key }); useRequired({ items, key }); usePlain({ items, key }); useArgs(items, key); const probe: never = key; }
{ const key = wrapAny((index: number) => `k-${index}` as const); use({ items, key }); useRequired({ items, key }); usePlain({ items, key }); useArgs(items, key); const probe: never = key; }
{ const key = wrapAny((index: number) => `k-${`i-${index}`}`); use({ items, key }); useRequired({ items, key }); usePlain({ items, key }); useArgs(items, key); const probe: never = key; }
{ const key = wrapFn((index: number) => `k-${index}`); use({ items, key }); useRequired({ items, key }); usePlain({ items, key }); useArgs(items, key); const probe: never = key; }
{ const key = wrapFn((index: number) => { return `k-${index}`; }); use({ items, key }); useRequired({ items, key }); usePlain({ items, key }); useArgs(items, key); const probe: never = key; }
{ const key = wrapFn(function (index: number) { return `k-${index}`; }); use({ items, key }); useRequired({ items, key }); usePlain({ items, key }); useArgs(items, key); const probe: never = key; }
{ const key = wrapFn((index: number) => "k"); use({ items, key }); useRequired({ items, key }); usePlain({ items, key }); useArgs(items, key); const probe: never = key; }
{ const key = wrapFn((index: number) => String(index)); use({ items, key }); useRequired({ items, key }); usePlain({ items, key }); useArgs(items, key); const probe: never = key; }
{ const key = wrapFn(() => `k`); use({ items, key }); useRequired({ items, key }); usePlain({ items, key }); useArgs(items, key); const probe: never = key; }
{ const key = wrapFn((index: number, other?: number) => `k-${index}-${other}`); use({ items, key }); useRequired({ items, key }); usePlain({ items, key }); useArgs(items, key); const probe: never = key; }
{ const key = wrapFn((index: number) => (index ? `a-${index}` : `b-${index}`)); use({ items, key }); useRequired({ items, key }); usePlain({ items, key }); useArgs(items, key); const probe: never = key; }
{ const key = wrapFn((index: number) => `k-${index}` as const); use({ items, key }); useRequired({ items, key }); usePlain({ items, key }); useArgs(items, key); const probe: never = key; }
{ const key = wrapFn((index: number) => `k-${`i-${index}`}`); use({ items, key }); useRequired({ items, key }); usePlain({ items, key }); useArgs(items, key); const probe: never = key; }
{ const key = (index: number) => `k-${index}`; use({ items, key }); useRequired({ items, key }); usePlain({ items, key }); useArgs(items, key); const probe: never = key; }
{ const key = (index: number) => { return `k-${index}`; }; use({ items, key }); useRequired({ items, key }); usePlain({ items, key }); useArgs(items, key); const probe: never = key; }
{ const key = function (index: number) { return `k-${index}`; }; use({ items, key }); useRequired({ items, key }); usePlain({ items, key }); useArgs(items, key); const probe: never = key; }
{ const key = (index: number) => "k"; use({ items, key }); useRequired({ items, key }); usePlain({ items, key }); useArgs(items, key); const probe: never = key; }
{ const key = (index: number) => String(index); use({ items, key }); useRequired({ items, key }); usePlain({ items, key }); useArgs(items, key); const probe: never = key; }
{ const key = () => `k`; use({ items, key }); useRequired({ items, key }); usePlain({ items, key }); useArgs(items, key); const probe: never = key; }
{ const key = (index: number, other?: number) => `k-${index}-${other}`; use({ items, key }); useRequired({ items, key }); usePlain({ items, key }); useArgs(items, key); const probe: never = key; }
{ const key = (index: number) => (index ? `a-${index}` : `b-${index}`); use({ items, key }); useRequired({ items, key }); usePlain({ items, key }); useArgs(items, key); const probe: never = key; }
{ const key = (index: number) => `k-${index}` as const; use({ items, key }); useRequired({ items, key }); usePlain({ items, key }); useArgs(items, key); const probe: never = key; }
{ const key = (index: number) => `k-${`i-${index}`}`; use({ items, key }); useRequired({ items, key }); usePlain({ items, key }); useArgs(items, key); const probe: never = key; }
{ const is = wrap((value: unknown) => typeof value === "string", []); useGuard({ items, is }); const probe: never = is; }
{ const is = wrap((value: unknown) => `${value}` === "a", []); useGuard({ items, is }); const probe: never = is; }
// tsc reports these
const direct = () => direct();
const throughWrap = wrap(() => throughWrap(), []);
const inTemplate = wrap((index: number) => `k-${inTemplate(index)}`, []);
function named(index: number) { return `k-${named(index)}`; }
const object = { method() { return `k-${object.method()}`; } };
use({ items, key: inTemplate });
const sentinel: number = "";
export {};
