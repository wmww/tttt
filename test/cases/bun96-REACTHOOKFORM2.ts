// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
import type { Box, Field } from './types';
type Local = { kind: 'local'; box: Box<number> };
declare const u: Field<number> | Local;

type Equal<A, B> = (<T>() => T extends A ? 1 : 2) extends (<T>() => T extends B ? 1 : 2) ? true : false;
const same: Equal<(typeof u)['box'], Box<number>> = true;
interface Mine extends Both {}
type Both = (typeof u)['box'];

u.box = {};
u.box = { value: 1, extra: 2 };
u.box.valu;
const text: string = u.box;
declare let both: Box<number> & Field<number>['box'];
both = null;
const sentinel: number = "";
// @filename: types.ts
export type Box<T> = { value: T };
export type Field<T> = { kind: 'field'; box: Box<T> };
