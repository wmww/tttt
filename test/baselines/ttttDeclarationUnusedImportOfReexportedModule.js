//// [test/cases/ttttDeclarationUnusedImportOfReexportedModule.ts] ////

//// [b.ts]
export interface Thing { a: number }
export const thing = 1;
//// [c.ts]
export interface Other { o: number }
//// [index.ts]
import * as b from './b';
import type { Thing } from './b';
export * from './b';
const x = b.thing;
export const y = 2;
//// [aug.ts]
import { Other } from './c';
export { Other };
declare module './c' {
    interface Other { extra: string }
}
//// [aug2.ts]
import * as c from './c';
export * from './c';
declare module './c' {
    interface Other { more: string }
}
//// [aug3.ts]
import * as d from './d';
import type { D } from './d';
export interface X { a: number }
//// [d.ts]
import {} from './aug3';
declare module './aug3' {
    interface X { b: number }
}
export interface D {}
//// [e.ts]
export interface E { e: number }
//// [f.ts]
export {};
declare module './e' {
    interface E { f: number }
}
//// [index2.ts]
import type { E } from './e';
export * from './e';
export const z = 1;


//// [b.js]
export const thing = 1;
//// [c.js]
export {};
//// [index.js]
import * as b from './b';
export * from './b';
const x = b.thing;
export const y = 2;
//// [aug.js]
export {};
//// [aug2.js]
export * from './c';
//// [d.js]
export {};
//// [aug3.js]
export {};
//// [e.js]
export {};
//// [f.js]
export {};
//// [index2.js]
export * from './e';
export const z = 1;


//// [b.d.ts]
export interface Thing {
    a: number;
}
export declare const thing = 1;
//// [c.d.ts]
export interface Other {
    o: number;
}
//// [index.d.ts]
export * from './b';
export declare const y = 2;
//// [aug.d.ts]
import { Other } from './c';
export { Other };
declare module './c' {
    interface Other {
        extra: string;
    }
}
//// [aug2.d.ts]
export * from './c';
declare module './c' {
    interface Other {
        more: string;
    }
}
//// [d.d.ts]
declare module './aug3' {
    interface X {
        b: number;
    }
}
export interface D {
}
//// [aug3.d.ts]
import './d';
export interface X {
    a: number;
}
//// [e.d.ts]
export interface E {
    e: number;
}
//// [f.d.ts]
export {};
declare module './e' {
    interface E {
        f: number;
    }
}
//// [index2.d.ts]
export * from './e';
export declare const z = 1;
