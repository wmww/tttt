// @noTypesAndSymbols: true
// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2023"], "skipLibCheck": true } }
// @filename: a.ts
declare function wrap<T extends Function>(callback: T, deps: readonly unknown[]): T;
declare const items: readonly string[];
declare const maybe: string | undefined;
declare function use1<T>(options: { items: readonly T[]; key?: (index: number) => string }): void;
{ const key = wrap((index: number) => `k-${index}`, []); use1({ items, key }); }
{ const key = wrap((index: number) => ({ a: index }), []); use1({ items, key }); }
{ const key = wrap((index: number) => () => index, []); use1({ items, key }); }
{ const key = wrap((index: number) => [index], []); use1({ items, key }); }
{ const key = wrap((index: number) => maybe ?? `k-${index}`, []); use1({ items, key }); }
{ const key = wrap((index: number) => maybe || `k-${index}`, []); use1({ items, key }); }
{ const key = wrap((index: number) => (index ? { a: 1 } : { b: 2 }), []); use1({ items, key }); }
{ const key = wrap((index: number) => ({ a: { b: [index] } }), []); use1({ items, key }); }
{ const key = wrap((index: number) => String(index), []); use1({ items, key }); }
{ const key = wrap((index: number) => index + 1, []); use1({ items, key }); }
{ const key = wrap((index: number) => ({ a: index } as const), []); use1({ items, key }); }
{ const key = wrap((index: number) => ({ a: index } satisfies object), []); use1({ items, key }); }
{ const key = wrap((index: number) => new Map([[index, index]]), []); use1({ items, key }); }
{ const key = wrap((index: number) => String.raw`k-${index}`, []); use1({ items, key }); }
{ const key = wrap((index: number) => Promise.resolve({ a: index }), []); use1({ items, key }); }
declare function use2<T>(options: { items: readonly T[]; key?: (index: number) => unknown }): void;
{ const key = wrap((index: number) => `k-${index}`, []); use2({ items, key }); }
{ const key = wrap((index: number) => ({ a: index }), []); use2({ items, key }); }
{ const key = wrap((index: number) => () => index, []); use2({ items, key }); }
{ const key = wrap((index: number) => [index], []); use2({ items, key }); }
{ const key = wrap((index: number) => maybe ?? `k-${index}`, []); use2({ items, key }); }
{ const key = wrap((index: number) => maybe || `k-${index}`, []); use2({ items, key }); }
{ const key = wrap((index: number) => (index ? { a: 1 } : { b: 2 }), []); use2({ items, key }); }
{ const key = wrap((index: number) => ({ a: { b: [index] } }), []); use2({ items, key }); }
{ const key = wrap((index: number) => String(index), []); use2({ items, key }); }
{ const key = wrap((index: number) => index + 1, []); use2({ items, key }); }
{ const key = wrap((index: number) => ({ a: index } as const), []); use2({ items, key }); }
{ const key = wrap((index: number) => ({ a: index } satisfies object), []); use2({ items, key }); }
{ const key = wrap((index: number) => new Map([[index, index]]), []); use2({ items, key }); }
{ const key = wrap((index: number) => String.raw`k-${index}`, []); use2({ items, key }); }
{ const key = wrap((index: number) => Promise.resolve({ a: index }), []); use2({ items, key }); }
declare function use3<T>(options: { items: readonly T[]; key: ((index: number) => unknown) | undefined }): void;
{ const key = wrap((index: number) => `k-${index}`, []); use3({ items, key }); }
{ const key = wrap((index: number) => ({ a: index }), []); use3({ items, key }); }
{ const key = wrap((index: number) => () => index, []); use3({ items, key }); }
{ const key = wrap((index: number) => [index], []); use3({ items, key }); }
{ const key = wrap((index: number) => maybe ?? `k-${index}`, []); use3({ items, key }); }
{ const key = wrap((index: number) => maybe || `k-${index}`, []); use3({ items, key }); }
{ const key = wrap((index: number) => (index ? { a: 1 } : { b: 2 }), []); use3({ items, key }); }
{ const key = wrap((index: number) => ({ a: { b: [index] } }), []); use3({ items, key }); }
{ const key = wrap((index: number) => String(index), []); use3({ items, key }); }
{ const key = wrap((index: number) => index + 1, []); use3({ items, key }); }
{ const key = wrap((index: number) => ({ a: index } as const), []); use3({ items, key }); }
{ const key = wrap((index: number) => ({ a: index } satisfies object), []); use3({ items, key }); }
{ const key = wrap((index: number) => new Map([[index, index]]), []); use3({ items, key }); }
{ const key = wrap((index: number) => String.raw`k-${index}`, []); use3({ items, key }); }
{ const key = wrap((index: number) => Promise.resolve({ a: index }), []); use3({ items, key }); }
declare function use4<T>(options: { items: readonly T[]; key: ((index: number) => unknown) | null }): void;
{ const key = wrap((index: number) => `k-${index}`, []); use4({ items, key }); }
{ const key = wrap((index: number) => ({ a: index }), []); use4({ items, key }); }
{ const key = wrap((index: number) => () => index, []); use4({ items, key }); }
{ const key = wrap((index: number) => [index], []); use4({ items, key }); }
{ const key = wrap((index: number) => maybe ?? `k-${index}`, []); use4({ items, key }); }
{ const key = wrap((index: number) => maybe || `k-${index}`, []); use4({ items, key }); }
{ const key = wrap((index: number) => (index ? { a: 1 } : { b: 2 }), []); use4({ items, key }); }
{ const key = wrap((index: number) => ({ a: { b: [index] } }), []); use4({ items, key }); }
{ const key = wrap((index: number) => String(index), []); use4({ items, key }); }
{ const key = wrap((index: number) => index + 1, []); use4({ items, key }); }
{ const key = wrap((index: number) => ({ a: index } as const), []); use4({ items, key }); }
{ const key = wrap((index: number) => ({ a: index } satisfies object), []); use4({ items, key }); }
{ const key = wrap((index: number) => new Map([[index, index]]), []); use4({ items, key }); }
{ const key = wrap((index: number) => String.raw`k-${index}`, []); use4({ items, key }); }
{ const key = wrap((index: number) => Promise.resolve({ a: index }), []); use4({ items, key }); }
declare function use5<T>(options: { items: readonly T[]; key: ((index: number) => unknown) | string }): void;
{ const key = wrap((index: number) => `k-${index}`, []); use5({ items, key }); }
{ const key = wrap((index: number) => ({ a: index }), []); use5({ items, key }); }
{ const key = wrap((index: number) => () => index, []); use5({ items, key }); }
{ const key = wrap((index: number) => [index], []); use5({ items, key }); }
{ const key = wrap((index: number) => maybe ?? `k-${index}`, []); use5({ items, key }); }
{ const key = wrap((index: number) => maybe || `k-${index}`, []); use5({ items, key }); }
{ const key = wrap((index: number) => (index ? { a: 1 } : { b: 2 }), []); use5({ items, key }); }
{ const key = wrap((index: number) => ({ a: { b: [index] } }), []); use5({ items, key }); }
{ const key = wrap((index: number) => String(index), []); use5({ items, key }); }
{ const key = wrap((index: number) => index + 1, []); use5({ items, key }); }
{ const key = wrap((index: number) => ({ a: index } as const), []); use5({ items, key }); }
{ const key = wrap((index: number) => ({ a: index } satisfies object), []); use5({ items, key }); }
{ const key = wrap((index: number) => new Map([[index, index]]), []); use5({ items, key }); }
{ const key = wrap((index: number) => String.raw`k-${index}`, []); use5({ items, key }); }
{ const key = wrap((index: number) => Promise.resolve({ a: index }), []); use5({ items, key }); }
declare function use6<T>(options: { items: readonly T[]; key: (index: number) => unknown }): void;
{ const key = wrap((index: number) => `k-${index}`, []); use6({ items, key }); }
{ const key = wrap((index: number) => ({ a: index }), []); use6({ items, key }); }
{ const key = wrap((index: number) => () => index, []); use6({ items, key }); }
{ const key = wrap((index: number) => [index], []); use6({ items, key }); }
{ const key = wrap((index: number) => maybe ?? `k-${index}`, []); use6({ items, key }); }
{ const key = wrap((index: number) => maybe || `k-${index}`, []); use6({ items, key }); }
{ const key = wrap((index: number) => (index ? { a: 1 } : { b: 2 }), []); use6({ items, key }); }
{ const key = wrap((index: number) => ({ a: { b: [index] } }), []); use6({ items, key }); }
{ const key = wrap((index: number) => String(index), []); use6({ items, key }); }
{ const key = wrap((index: number) => index + 1, []); use6({ items, key }); }
{ const key = wrap((index: number) => ({ a: index } as const), []); use6({ items, key }); }
{ const key = wrap((index: number) => ({ a: index } satisfies object), []); use6({ items, key }); }
{ const key = wrap((index: number) => new Map([[index, index]]), []); use6({ items, key }); }
{ const key = wrap((index: number) => String.raw`k-${index}`, []); use6({ items, key }); }
{ const key = wrap((index: number) => Promise.resolve({ a: index }), []); use6({ items, key }); }
declare function use7<T>(options: { items: readonly T[]; key?: (index: number, other: string) => unknown }): void;
{ const key = wrap((index: number, other: string) => `k-${index}`, []); use7({ items, key }); }
{ const key = wrap((index: number, other: string) => ({ a: index }), []); use7({ items, key }); }
{ const key = wrap((index: number, other: string) => () => index, []); use7({ items, key }); }
{ const key = wrap((index: number, other: string) => [index], []); use7({ items, key }); }
{ const key = wrap((index: number, other: string) => maybe ?? `k-${index}`, []); use7({ items, key }); }
{ const key = wrap((index: number, other: string) => maybe || `k-${index}`, []); use7({ items, key }); }
{ const key = wrap((index: number, other: string) => (index ? { a: 1 } : { b: 2 }), []); use7({ items, key }); }
{ const key = wrap((index: number, other: string) => ({ a: { b: [index] } }), []); use7({ items, key }); }
{ const key = wrap((index: number, other: string) => String(index), []); use7({ items, key }); }
{ const key = wrap((index: number, other: string) => index + 1, []); use7({ items, key }); }
{ const key = wrap((index: number, other: string) => ({ a: index } as const), []); use7({ items, key }); }
{ const key = wrap((index: number, other: string) => ({ a: index } satisfies object), []); use7({ items, key }); }
{ const key = wrap((index: number, other: string) => new Map([[index, index]]), []); use7({ items, key }); }
{ const key = wrap((index: number, other: string) => String.raw`k-${index}`, []); use7({ items, key }); }
{ const key = wrap((index: number, other: string) => Promise.resolve({ a: index }), []); use7({ items, key }); }
declare function use8<T>(options: { items: readonly T[]; key?: () => unknown }): void;
{ const key = wrap((index: number) => `k-${index}`, []); use8({ items, key }); }
{ const key = wrap((index: number) => ({ a: index }), []); use8({ items, key }); }
{ const key = wrap((index: number) => () => index, []); use8({ items, key }); }
{ const key = wrap((index: number) => [index], []); use8({ items, key }); }
{ const key = wrap((index: number) => maybe ?? `k-${index}`, []); use8({ items, key }); }
{ const key = wrap((index: number) => maybe || `k-${index}`, []); use8({ items, key }); }
{ const key = wrap((index: number) => (index ? { a: 1 } : { b: 2 }), []); use8({ items, key }); }
{ const key = wrap((index: number) => ({ a: { b: [index] } }), []); use8({ items, key }); }
{ const key = wrap((index: number) => String(index), []); use8({ items, key }); }
{ const key = wrap((index: number) => index + 1, []); use8({ items, key }); }
{ const key = wrap((index: number) => ({ a: index } as const), []); use8({ items, key }); }
{ const key = wrap((index: number) => ({ a: index } satisfies object), []); use8({ items, key }); }
{ const key = wrap((index: number) => new Map([[index, index]]), []); use8({ items, key }); }
{ const key = wrap((index: number) => String.raw`k-${index}`, []); use8({ items, key }); }
{ const key = wrap((index: number) => Promise.resolve({ a: index }), []); use8({ items, key }); }
const sentinel: number = "";
export {};
