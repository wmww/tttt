// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
function f6<T extends [((x: number) => void)?, ((x: string) => void)?]>([a = x => {}]: T) {}
const sentinel: number = "";
