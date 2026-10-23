// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
export declare function p({ a: b }): typeof b;
export declare function p2({ a: b }, c: typeof b): void;
export namespace NS { export declare function q({ a: b }): void; }
NS.q({ a: 1 });
export const sentinel: number = "";
