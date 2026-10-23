// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
class A1 { [+2]: number; constructor() { this["+2"] = 1; } }
class B5 { [-2]: number; constructor() { this["-2"] = 1; } }
class B4 { [-2]: number; constructor() { this[-2] = 1; } }
class A4 { [2]: number; constructor() { this[2] = 1; } }
const sentinel: number = "";
