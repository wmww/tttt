// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
declare class A {} declare class B {} interface I {}
type T1 = ({a = class extends A, B {}}: any) => void;
type T2 = ({a = class extends A extends B {}}: any) => void;
type T3 = ({a = class implements I extends A {}}: any) => void;
type T4 = ({a = class <> {}}: any) => void;
type T5 = ({a = class { m<>() {} }}: any) => void;
type T6 = ({a = (public a) => 1}: any) => void;
type OK = <T>({a = class extends A, B {}}: any) => void;
const sentinel: number = "";
