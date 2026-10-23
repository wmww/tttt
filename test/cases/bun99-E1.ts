// @noTypesAndSymbols: true
// @filename: tsconfig.json
{"compilerOptions": {"strict": true, "noEmit": true, "module": "ESNext", "moduleResolution": "bundler", "types": [], "lib": ["es2022"], "skipLibCheck": true}, "files": ["m0.ts", "m4.ts", "m10.ts", "m11.ts", "m14.ts", "m15.ts", "m28.ts", "m29.ts", "m30.ts", "m42.ts", "m43.ts", "m45.ts", "m50.ts", "m53.ts", "m60.ts", "m61.ts", "m63.ts", "m64.ts", "m66.ts", "m77.ts", "m78.ts", "m81.ts", "m91.ts", "m92.ts", "m98.ts", "m99.ts", "m100.ts", "m105.ts", "m106.ts", "m108.ts", "m117.ts", "m120.ts", "m132.ts", "m150.ts", "m162.ts", "m165.ts", "m178.ts", "m183.ts", "m189.ts", "m196.ts", "m197.ts", "m200.ts", "m203.ts", "m204.ts", "m220.ts", "m230.ts", "m231.ts", "m245.ts", "m246.ts", "m249.ts", "m250.ts", "m252.ts", "m253.ts", "m267.ts", "m273.ts", "m276.ts", "m280.ts", "m281.ts", "m288.ts", "m291.ts", "m294.ts", "m295.ts", "m297.ts", "m301.ts", "m302.ts", "m312.ts", "m315.ts", "m330.ts", "m334.ts", "use.ts"]}
// @filename: m0.ts
// alias alias alias
export type N = "get" | "post";
export type N = "get" | "post";
export type N = "get" | "post";
const v: never = N;
const t: never = null! as N;
type Q = typeof N;
const q: never = null! as Q;
// @filename: m10.ts
// alias typeof interface
export type N = "get" | "post";
export type N = typeof N;
export interface N { i: number }
const v: never = N;
const t: never = null! as N;
type Q = typeof N;
const q: never = null! as Q;
// @filename: m100.ts
// const alias const
export declare const N: { a: number };
export type N = "get" | "post";
export declare const N: { a: number };
const v: never = N;
const t: never = null! as N;
type Q = typeof N;
const q: never = null! as Q;
// @filename: m105.ts
// const typeof alias
export declare const N: { a: number };
export type N = typeof N;
export type N = "get" | "post";
const v: never = N;
const t: never = null! as N;
type Q = typeof N;
const q: never = null! as Q;
// @filename: m106.ts
// const typeof typeof
export declare const N: { a: number };
export type N = typeof N;
export type N = typeof N;
const v: never = N;
const t: never = null! as N;
type Q = typeof N;
const q: never = null! as Q;
// @filename: m108.ts
// const typeof interface
export declare const N: { a: number };
export type N = typeof N;
export interface N { i: number }
const v: never = N;
const t: never = null! as N;
type Q = typeof N;
const q: never = null! as Q;
// @filename: m11.ts
// alias typeof function
export type N = "get" | "post";
export type N = typeof N;
export declare function N(): string;
const v: never = N;
const t: never = null! as N;
type Q = typeof N;
const q: never = null! as Q;
// @filename: m117.ts
// const const class
export declare const N: { a: number };
export declare const N: { a: number };
export declare class N { c: number }
const v: never = N;
const t: never = null! as N;
type Q = typeof N;
const q: never = null! as Q;
// @filename: m120.ts
// const interface typeof
export declare const N: { a: number };
export interface N { i: number }
export type N = typeof N;
const v: never = N;
const t: never = null! as N;
type Q = typeof N;
const q: never = null! as Q;
// @filename: m132.ts
// const function namespace
export declare const N: { a: number };
export declare function N(): string;
export declare namespace N { const inner: boolean }
const v: never = N;
const t: never = null! as N;
type Q = typeof N;
const q: never = null! as Q;
// @filename: m14.ts
// alias const alias
export type N = "get" | "post";
export declare const N: { a: number };
export type N = "get" | "post";
const v: never = N;
const t: never = null! as N;
type Q = typeof N;
const q: never = null! as Q;
// @filename: m15.ts
// alias const typeof
export type N = "get" | "post";
export declare const N: { a: number };
export type N = typeof N;
const v: never = N;
const t: never = null! as N;
type Q = typeof N;
const q: never = null! as Q;
// @filename: m150.ts
// interface alias interface
export interface N { i: number }
export type N = "get" | "post";
export interface N { i: number }
const v: never = N;
const t: never = null! as N;
type Q = typeof N;
const q: never = null! as Q;
// @filename: m162.ts
// interface const typeof
export interface N { i: number }
export declare const N: { a: number };
export type N = typeof N;
const v: never = N;
const t: never = null! as N;
type Q = typeof N;
const q: never = null! as Q;
// @filename: m165.ts
// interface const function
export interface N { i: number }
export declare const N: { a: number };
export declare function N(): string;
const v: never = N;
const t: never = null! as N;
type Q = typeof N;
const q: never = null! as Q;
// @filename: m178.ts
// interface function interface
export interface N { i: number }
export declare function N(): string;
export interface N { i: number }
const v: never = N;
const t: never = null! as N;
type Q = typeof N;
const q: never = null! as Q;
// @filename: m183.ts
// interface class typeof
export interface N { i: number }
export declare class N { c: number }
export type N = typeof N;
const v: never = N;
const t: never = null! as N;
type Q = typeof N;
const q: never = null! as Q;
// @filename: m189.ts
// interface namespace alias
export interface N { i: number }
export declare namespace N { const inner: boolean }
export type N = "get" | "post";
const v: never = N;
const t: never = null! as N;
type Q = typeof N;
const q: never = null! as Q;
// @filename: m196.ts
// function alias alias
export declare function N(): string;
export type N = "get" | "post";
export type N = "get" | "post";
const v: never = N;
const t: never = null! as N;
type Q = typeof N;
const q: never = null! as Q;
// @filename: m197.ts
// function alias typeof
export declare function N(): string;
export type N = "get" | "post";
export type N = typeof N;
const v: never = N;
const t: never = null! as N;
type Q = typeof N;
const q: never = null! as Q;
// @filename: m200.ts
// function alias function
export declare function N(): string;
export type N = "get" | "post";
export declare function N(): string;
const v: never = N;
const t: never = null! as N;
type Q = typeof N;
const q: never = null! as Q;
// @filename: m203.ts
// function typeof alias
export declare function N(): string;
export type N = typeof N;
export type N = "get" | "post";
const v: never = N;
const t: never = null! as N;
type Q = typeof N;
const q: never = null! as Q;
// @filename: m204.ts
// function typeof typeof
export declare function N(): string;
export type N = typeof N;
export type N = typeof N;
const v: never = N;
const t: never = null! as N;
type Q = typeof N;
const q: never = null! as Q;
// @filename: m220.ts
// function interface interface
export declare function N(): string;
export interface N { i: number }
export interface N { i: number }
const v: never = N;
const t: never = null! as N;
type Q = typeof N;
const q: never = null! as Q;
// @filename: m230.ts
// function function namespace
export declare function N(): string;
export declare function N(): string;
export declare namespace N { const inner: boolean }
const v: never = N;
const t: never = null! as N;
type Q = typeof N;
const q: never = null! as Q;
// @filename: m231.ts
// function class alias
export declare function N(): string;
export declare class N { c: number }
export type N = "get" | "post";
const v: never = N;
const t: never = null! as N;
type Q = typeof N;
const q: never = null! as Q;
// @filename: m245.ts
// class alias alias
export declare class N { c: number }
export type N = "get" | "post";
export type N = "get" | "post";
const v: never = N;
const t: never = null! as N;
type Q = typeof N;
const q: never = null! as Q;
// @filename: m246.ts
// class alias typeof
export declare class N { c: number }
export type N = "get" | "post";
export type N = typeof N;
const v: never = N;
const t: never = null! as N;
type Q = typeof N;
const q: never = null! as Q;
// @filename: m249.ts
// class alias function
export declare class N { c: number }
export type N = "get" | "post";
export declare function N(): string;
const v: never = N;
const t: never = null! as N;
type Q = typeof N;
const q: never = null! as Q;
// @filename: m250.ts
// class alias class
export declare class N { c: number }
export type N = "get" | "post";
export declare class N { c: number }
const v: never = N;
const t: never = null! as N;
type Q = typeof N;
const q: never = null! as Q;
// @filename: m252.ts
// class typeof alias
export declare class N { c: number }
export type N = typeof N;
export type N = "get" | "post";
const v: never = N;
const t: never = null! as N;
type Q = typeof N;
const q: never = null! as Q;
// @filename: m253.ts
// class typeof typeof
export declare class N { c: number }
export type N = typeof N;
export type N = typeof N;
const v: never = N;
const t: never = null! as N;
type Q = typeof N;
const q: never = null! as Q;
// @filename: m267.ts
// class interface typeof
export declare class N { c: number }
export interface N { i: number }
export type N = typeof N;
const v: never = N;
const t: never = null! as N;
type Q = typeof N;
const q: never = null! as Q;
// @filename: m273.ts
// class function alias
export declare class N { c: number }
export declare function N(): string;
export type N = "get" | "post";
const v: never = N;
const t: never = null! as N;
type Q = typeof N;
const q: never = null! as Q;
// @filename: m276.ts
// class function interface
export declare class N { c: number }
export declare function N(): string;
export interface N { i: number }
const v: never = N;
const t: never = null! as N;
type Q = typeof N;
const q: never = null! as Q;
// @filename: m28.ts
// alias function alias
export type N = "get" | "post";
export declare function N(): string;
export type N = "get" | "post";
const v: never = N;
const t: never = null! as N;
type Q = typeof N;
const q: never = null! as Q;
// @filename: m280.ts
// class class alias
export declare class N { c: number }
export declare class N { c: number }
export type N = "get" | "post";
const v: never = N;
const t: never = null! as N;
type Q = typeof N;
const q: never = null! as Q;
// @filename: m281.ts
// class class typeof
export declare class N { c: number }
export declare class N { c: number }
export type N = typeof N;
const v: never = N;
const t: never = null! as N;
type Q = typeof N;
const q: never = null! as Q;
// @filename: m288.ts
// class namespace typeof
export declare class N { c: number }
export declare namespace N { const inner: boolean }
export type N = typeof N;
const v: never = N;
const t: never = null! as N;
type Q = typeof N;
const q: never = null! as Q;
// @filename: m29.ts
// alias function typeof
export type N = "get" | "post";
export declare function N(): string;
export type N = typeof N;
const v: never = N;
const t: never = null! as N;
type Q = typeof N;
const q: never = null! as Q;
// @filename: m291.ts
// class namespace function
export declare class N { c: number }
export declare namespace N { const inner: boolean }
export declare function N(): string;
const v: never = N;
const t: never = null! as N;
type Q = typeof N;
const q: never = null! as Q;
// @filename: m294.ts
// namespace alias alias
export declare namespace N { const inner: boolean }
export type N = "get" | "post";
export type N = "get" | "post";
const v: never = N;
const t: never = null! as N;
type Q = typeof N;
const q: never = null! as Q;
// @filename: m295.ts
// namespace alias typeof
export declare namespace N { const inner: boolean }
export type N = "get" | "post";
export type N = typeof N;
const v: never = N;
const t: never = null! as N;
type Q = typeof N;
const q: never = null! as Q;
// @filename: m297.ts
// namespace alias interface
export declare namespace N { const inner: boolean }
export type N = "get" | "post";
export interface N { i: number }
const v: never = N;
const t: never = null! as N;
type Q = typeof N;
const q: never = null! as Q;
// @filename: m30.ts
// alias function const
export type N = "get" | "post";
export declare function N(): string;
export declare const N: { a: number };
const v: never = N;
const t: never = null! as N;
type Q = typeof N;
const q: never = null! as Q;
// @filename: m301.ts
// namespace typeof alias
export declare namespace N { const inner: boolean }
export type N = typeof N;
export type N = "get" | "post";
const v: never = N;
const t: never = null! as N;
type Q = typeof N;
const q: never = null! as Q;
// @filename: m302.ts
// namespace typeof typeof
export declare namespace N { const inner: boolean }
export type N = typeof N;
export type N = typeof N;
const v: never = N;
const t: never = null! as N;
type Q = typeof N;
const q: never = null! as Q;
// @filename: m312.ts
// namespace const function
export declare namespace N { const inner: boolean }
export declare const N: { a: number };
export declare function N(): string;
const v: never = N;
const t: never = null! as N;
type Q = typeof N;
const q: never = null! as Q;
// @filename: m315.ts
// namespace interface alias
export declare namespace N { const inner: boolean }
export interface N { i: number }
export type N = "get" | "post";
const v: never = N;
const t: never = null! as N;
type Q = typeof N;
const q: never = null! as Q;
// @filename: m330.ts
// namespace class typeof
export declare namespace N { const inner: boolean }
export declare class N { c: number }
export type N = typeof N;
const v: never = N;
const t: never = null! as N;
type Q = typeof N;
const q: never = null! as Q;
// @filename: m334.ts
// namespace class class
export declare namespace N { const inner: boolean }
export declare class N { c: number }
export declare class N { c: number }
const v: never = N;
const t: never = null! as N;
type Q = typeof N;
const q: never = null! as Q;
// @filename: m4.ts
// alias alias function
export type N = "get" | "post";
export type N = "get" | "post";
export declare function N(): string;
const v: never = N;
const t: never = null! as N;
type Q = typeof N;
const q: never = null! as Q;
// @filename: m42.ts
// alias namespace alias
export type N = "get" | "post";
export declare namespace N { const inner: boolean }
export type N = "get" | "post";
const v: never = N;
const t: never = null! as N;
type Q = typeof N;
const q: never = null! as Q;
// @filename: m43.ts
// alias namespace typeof
export type N = "get" | "post";
export declare namespace N { const inner: boolean }
export type N = typeof N;
const v: never = N;
const t: never = null! as N;
type Q = typeof N;
const q: never = null! as Q;
// @filename: m45.ts
// alias namespace interface
export type N = "get" | "post";
export declare namespace N { const inner: boolean }
export interface N { i: number }
const v: never = N;
const t: never = null! as N;
type Q = typeof N;
const q: never = null! as Q;
// @filename: m50.ts
// typeof alias typeof
export type N = typeof N;
export type N = "get" | "post";
export type N = typeof N;
const v: never = N;
const t: never = null! as N;
type Q = typeof N;
const q: never = null! as Q;
// @filename: m53.ts
// typeof alias function
export type N = typeof N;
export type N = "get" | "post";
export declare function N(): string;
const v: never = N;
const t: never = null! as N;
type Q = typeof N;
const q: never = null! as Q;
// @filename: m60.ts
// typeof typeof function
export type N = typeof N;
export type N = typeof N;
export declare function N(): string;
const v: never = N;
const t: never = null! as N;
type Q = typeof N;
const q: never = null! as Q;
// @filename: m61.ts
// typeof typeof class
export type N = typeof N;
export type N = typeof N;
export declare class N { c: number }
const v: never = N;
const t: never = null! as N;
type Q = typeof N;
const q: never = null! as Q;
// @filename: m63.ts
// typeof const alias
export type N = typeof N;
export declare const N: { a: number };
export type N = "get" | "post";
const v: never = N;
const t: never = null! as N;
type Q = typeof N;
const q: never = null! as Q;
// @filename: m64.ts
// typeof const typeof
export type N = typeof N;
export declare const N: { a: number };
export type N = typeof N;
const v: never = N;
const t: never = null! as N;
type Q = typeof N;
const q: never = null! as Q;
// @filename: m66.ts
// typeof const interface
export type N = typeof N;
export declare const N: { a: number };
export interface N { i: number }
const v: never = N;
const t: never = null! as N;
type Q = typeof N;
const q: never = null! as Q;
// @filename: m77.ts
// typeof function alias
export type N = typeof N;
export declare function N(): string;
export type N = "get" | "post";
const v: never = N;
const t: never = null! as N;
type Q = typeof N;
const q: never = null! as Q;
// @filename: m78.ts
// typeof function typeof
export type N = typeof N;
export declare function N(): string;
export type N = typeof N;
const v: never = N;
const t: never = null! as N;
type Q = typeof N;
const q: never = null! as Q;
// @filename: m81.ts
// typeof function function
export type N = typeof N;
export declare function N(): string;
export declare function N(): string;
const v: never = N;
const t: never = null! as N;
type Q = typeof N;
const q: never = null! as Q;
// @filename: m91.ts
// typeof namespace alias
export type N = typeof N;
export declare namespace N { const inner: boolean }
export type N = "get" | "post";
const v: never = N;
const t: never = null! as N;
type Q = typeof N;
const q: never = null! as Q;
// @filename: m92.ts
// typeof namespace typeof
export type N = typeof N;
export declare namespace N { const inner: boolean }
export type N = typeof N;
const v: never = N;
const t: never = null! as N;
type Q = typeof N;
const q: never = null! as Q;
// @filename: m98.ts
// const alias alias
export declare const N: { a: number };
export type N = "get" | "post";
export type N = "get" | "post";
const v: never = N;
const t: never = null! as N;
type Q = typeof N;
const q: never = null! as Q;
// @filename: m99.ts
// const alias typeof
export declare const N: { a: number };
export type N = "get" | "post";
export type N = typeof N;
const v: never = N;
const t: never = null! as N;
type Q = typeof N;
const q: never = null! as Q;
// @filename: use.ts
import { N as N0 } from "./m0";
import { N as N1 } from "./m4";
import { N as N2 } from "./m10";
import { N as N3 } from "./m11";
import { N as N4 } from "./m14";
import { N as N5 } from "./m15";
import { N as N6 } from "./m28";
import { N as N7 } from "./m29";
import { N as N8 } from "./m30";
import { N as N9 } from "./m42";
import { N as N10 } from "./m43";
import { N as N11 } from "./m45";
import { N as N12 } from "./m50";
import { N as N13 } from "./m53";
import { N as N14 } from "./m60";
import { N as N15 } from "./m61";
import { N as N16 } from "./m63";
import { N as N17 } from "./m64";
import { N as N18 } from "./m66";
import { N as N19 } from "./m77";
import { N as N20 } from "./m78";
import { N as N21 } from "./m81";
import { N as N22 } from "./m91";
import { N as N23 } from "./m92";
import { N as N24 } from "./m98";
import { N as N25 } from "./m99";
import { N as N26 } from "./m100";
import { N as N27 } from "./m105";
import { N as N28 } from "./m106";
import { N as N29 } from "./m108";
import { N as N30 } from "./m117";
import { N as N31 } from "./m120";
import { N as N32 } from "./m132";
import { N as N33 } from "./m150";
import { N as N34 } from "./m162";
import { N as N35 } from "./m165";
import { N as N36 } from "./m178";
import { N as N37 } from "./m183";
import { N as N38 } from "./m189";
import { N as N39 } from "./m196";
import { N as N40 } from "./m197";
import { N as N41 } from "./m200";
import { N as N42 } from "./m203";
import { N as N43 } from "./m204";
import { N as N44 } from "./m220";
import { N as N45 } from "./m230";
import { N as N46 } from "./m231";
import { N as N47 } from "./m245";
import { N as N48 } from "./m246";
import { N as N49 } from "./m249";
import { N as N50 } from "./m250";
import { N as N51 } from "./m252";
import { N as N52 } from "./m253";
import { N as N53 } from "./m267";
import { N as N54 } from "./m273";
import { N as N55 } from "./m276";
import { N as N56 } from "./m280";
import { N as N57 } from "./m281";
import { N as N58 } from "./m288";
import { N as N59 } from "./m291";
import { N as N60 } from "./m294";
import { N as N61 } from "./m295";
import { N as N62 } from "./m297";
import { N as N63 } from "./m301";
import { N as N64 } from "./m302";
import { N as N65 } from "./m312";
import { N as N66 } from "./m315";
import { N as N67 } from "./m330";
import { N as N68 } from "./m334";
{ const v: never = N0; const t: never = null! as N0; }
{ const v: never = N1; const t: never = null! as N1; }
{ const v: never = N2; const t: never = null! as N2; }
{ const v: never = N3; const t: never = null! as N3; }
{ const v: never = N4; const t: never = null! as N4; }
{ const v: never = N5; const t: never = null! as N5; }
{ const v: never = N6; const t: never = null! as N6; }
{ const v: never = N7; const t: never = null! as N7; }
{ const v: never = N8; const t: never = null! as N8; }
{ const v: never = N9; const t: never = null! as N9; }
{ const v: never = N10; const t: never = null! as N10; }
{ const v: never = N11; const t: never = null! as N11; }
{ const v: never = N12; const t: never = null! as N12; }
{ const v: never = N13; const t: never = null! as N13; }
{ const v: never = N14; const t: never = null! as N14; }
{ const v: never = N15; const t: never = null! as N15; }
{ const v: never = N16; const t: never = null! as N16; }
{ const v: never = N17; const t: never = null! as N17; }
{ const v: never = N18; const t: never = null! as N18; }
{ const v: never = N19; const t: never = null! as N19; }
{ const v: never = N20; const t: never = null! as N20; }
{ const v: never = N21; const t: never = null! as N21; }
{ const v: never = N22; const t: never = null! as N22; }
{ const v: never = N23; const t: never = null! as N23; }
{ const v: never = N24; const t: never = null! as N24; }
{ const v: never = N25; const t: never = null! as N25; }
{ const v: never = N26; const t: never = null! as N26; }
{ const v: never = N27; const t: never = null! as N27; }
{ const v: never = N28; const t: never = null! as N28; }
{ const v: never = N29; const t: never = null! as N29; }
{ const v: never = N30; const t: never = null! as N30; }
{ const v: never = N31; const t: never = null! as N31; }
{ const v: never = N32; const t: never = null! as N32; }
{ const v: never = N33; const t: never = null! as N33; }
{ const v: never = N34; const t: never = null! as N34; }
{ const v: never = N35; const t: never = null! as N35; }
{ const v: never = N36; const t: never = null! as N36; }
{ const v: never = N37; const t: never = null! as N37; }
{ const v: never = N38; const t: never = null! as N38; }
{ const v: never = N39; const t: never = null! as N39; }
{ const v: never = N40; const t: never = null! as N40; }
{ const v: never = N41; const t: never = null! as N41; }
{ const v: never = N42; const t: never = null! as N42; }
{ const v: never = N43; const t: never = null! as N43; }
{ const v: never = N44; const t: never = null! as N44; }
{ const v: never = N45; const t: never = null! as N45; }
{ const v: never = N46; const t: never = null! as N46; }
{ const v: never = N47; const t: never = null! as N47; }
{ const v: never = N48; const t: never = null! as N48; }
{ const v: never = N49; const t: never = null! as N49; }
{ const v: never = N50; const t: never = null! as N50; }
{ const v: never = N51; const t: never = null! as N51; }
{ const v: never = N52; const t: never = null! as N52; }
{ const v: never = N53; const t: never = null! as N53; }
{ const v: never = N54; const t: never = null! as N54; }
{ const v: never = N55; const t: never = null! as N55; }
{ const v: never = N56; const t: never = null! as N56; }
{ const v: never = N57; const t: never = null! as N57; }
{ const v: never = N58; const t: never = null! as N58; }
{ const v: never = N59; const t: never = null! as N59; }
{ const v: never = N60; const t: never = null! as N60; }
{ const v: never = N61; const t: never = null! as N61; }
{ const v: never = N62; const t: never = null! as N62; }
{ const v: never = N63; const t: never = null! as N63; }
{ const v: never = N64; const t: never = null! as N64; }
{ const v: never = N65; const t: never = null! as N65; }
{ const v: never = N66; const t: never = null! as N66; }
{ const v: never = N67; const t: never = null! as N67; }
{ const v: never = N68; const t: never = null! as N68; }
const sentinel: number = "";
