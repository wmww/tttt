// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
declare const k: string; declare function fn(): string; declare const o: { p: string };
function f1({ [1   +  2]: a }: any) {}
function f2({ [fn( )]: a }: any) {}
function f3({ [`a${ k }b`]: a }: any) {}
function f4({ [o [ "p" ]]: a }: any) {}
function f5({ [ /*c*/ k   + /*d*/ "x" ]: a }: any) {}
function f6({ [0x10 + 1]: a }: any) {}
function f7({ [1
  + 2]: a }: any) {}
const n1: number = f1;
const n2: number = f2;
const n3: number = f3;
const n4: number = f4;
const n5: number = f5;
const n6: number = f6;
const n7: number = f7;
const sentinel: number = "";
