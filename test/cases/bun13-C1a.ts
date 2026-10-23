// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
type Q = keyof Partial<D>;
interface D extends Partial<D> { m(): void }
const sentinel: number = "";
