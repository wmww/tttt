// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2023"] } }
// @filename: a.ts
class K { k = 1 }
declare function id<T>(value: T): T;
declare function memo<T>(factory: () => T): T;
declare const k: NoInfer<K | string | undefined> | undefined;
export const v1 = id(k instanceof K ? { s: 1, ...k } : undefined);
export const v2 = memo(() => (k instanceof K ? { ...k, s: 1 } : undefined));
export const v3 = k instanceof K ? { s: 1, ...k } : undefined;
const sentinel: number = "";
