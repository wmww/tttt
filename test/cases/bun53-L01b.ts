// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
enum F1 { ["a" as const] = 1, B = 2 }
enum F2 { ["a"!] = 1, B = 2 }
enum F3 { ["a" satisfies string] = 1, B = 2 }
enum F4 { [<string>"a"] = 1, B = 2 }
enum F5 { [1 as const] = 1, B = 2 }
enum F6 { [1!] = 1, B = 2 }
const k1: keyof typeof F1 = 0;
const sentinel: number = "";
