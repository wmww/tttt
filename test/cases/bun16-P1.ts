// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
declare function n(x: never): void;
declare function p7<T>(mk: () => ((x: T) => string)[], other: (y: T) => void, v: T): T;
const r1 = p7(() => [...[x => 1]], y => {}, 1); n(r1);
declare function p8<T>(mk: () => T, other: (y: T) => void): T;
const r2 = p8(() => [...[x => 1]], y => {}); n(r2);
const sentinel: number = "";
