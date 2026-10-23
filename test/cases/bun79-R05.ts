// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
const a1: "" = `${2 ** -25}`;
const a2: never = 2.98023223876953125e-8;
const a3: never = 713680496268224.25;
const a4: never = 258555422826684.625;
enum E { B = 1 / 33554432 } const a5: "" = `${E.B}`;
const o = { 713680496268224.25: 1 }; o["713680496268224.2"]; o["713680496268224.3"];
const sentinel: number = "";
