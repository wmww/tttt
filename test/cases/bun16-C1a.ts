// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
interface Ia extends Pick<typeof xa, "a"> {}
declare const ya: Ia | undefined;
const xa = ya!;
const sentinel: number = "";
