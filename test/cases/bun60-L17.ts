// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
class C { private constructor() {} x = 1; }
type CT = typeof C;
interface IC extends CT {}
declare const B1: IC;
class D1 extends B1 {}
interface IC6 extends IC { y: 1 }
declare const B6: IC6;
class D6 extends B6 {}
class D2 extends C {}
const sentinel: number = "";
