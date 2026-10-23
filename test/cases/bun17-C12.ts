// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
type TA = [x: number, y: string];
type TB = [x: number, y: boolean];
declare let s: [x: number, y: symbol];
let t: TA | TB = s;
const sentinel: number = "";
