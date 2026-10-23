// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
const g1 = () => {}; g1.a = "b" as const; g1[k1] = 1; declare const k1: (typeof g1)["a"];
const g2 = function () {}; g2.a = "b" as const; g2[k2] = 1; declare const k2: (typeof g2)["a"];
function g3() {} g3.a = "b" as const; g3[k3] = 1; declare const k3: (typeof g3)["a"];
declare const k4: (typeof g4)["a"]; const g4 = () => {}; g4.a = "b" as const; g4[k4] = 1;
const g5 = () => {}; g5.a = "b" as const; g5[k5] = 1; const k5 = "c"; g5.c;
const g6 = () => {}; g6.a = "b" as const; g6[k6] = 1; declare const k6: "c"; g6.c; g6.b;
const g7 = () => {}; g7[k7] = 1; declare const k7: ReturnType<typeof g7> extends void ? "c" : "d"; g7.c;
const g8 = () => {}; g8.a = "b" as const; declare const k8: (typeof g8)["a"]; g8[k8] = 1; g8.b;
const sentinel: number = "";
