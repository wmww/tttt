// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
export type PickByType<T> = { [K in keyof T extends T[K] ? K : never]: T[K] };
const sentinel: number = "";
