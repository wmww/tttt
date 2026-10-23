// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
declare class G1 { static get prototype(): number; }
const a1: number = G1.prototype;
declare class G2 { static set prototype(v: number); }
G2.prototype = 1; G2.prototype = new G2();
declare class G4 { static accessor prototype: number; }
const a5: number = G4.prototype;
const s1 = { ...G1 }; const a14: null = s1;
class C4 { static get prototype() { return 1; } }
const p4: null = C4.prototype;
const sentinel: number = "";
