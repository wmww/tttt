// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
declare class G6 { static readonly prototype: G6; }
G6.prototype = new G6();
declare class G7 { private static prototype: G7; }
G7.prototype;
type K7 = keyof typeof G7; const a13: null = null! as K7;
declare class G9 { protected static prototype: G9; }
G9.prototype;
declare class G5 { static prototype: G5; }
G5.prototype = new G5();
const sentinel: number = "";
