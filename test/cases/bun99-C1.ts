// @noTypesAndSymbols: true
// @filename: tsconfig.json
{"compilerOptions": {"strict": true, "noEmit": true, "module": "ESNext", "moduleResolution": "bundler", "target": "ES2022", "types": [], "esModuleInterop": true, "skipLibCheck": true}, "files": ["index.ts", "u0_named.ts", "u0_type-only.ts", "u0_reexport.ts", "u0_namespace.ts", "u0_through.ts", "u1_named.ts", "u1_type-only.ts", "u1_reexport.ts", "u1_namespace.ts", "u1_through.ts", "u2_named.ts", "u2_type-only.ts", "u2_reexport.ts", "u2_namespace.ts", "u2_through.ts", "u3_named.ts", "u3_type-only.ts", "u3_reexport.ts", "u3_namespace.ts", "u3_through.ts", "u4_named.ts", "u4_type-only.ts", "u4_reexport.ts", "u4_namespace.ts", "u4_through.ts", "u6_named.ts", "u6_type-only.ts", "u6_reexport.ts", "u6_namespace.ts", "u6_through.ts", "u7_named.ts", "u7_type-only.ts", "u7_reexport.ts", "u7_namespace.ts", "u7_through.ts", "u8_named.ts", "u8_type-only.ts", "u8_reexport.ts", "u8_namespace.ts", "u8_through.ts", "u10_named.ts", "u10_type-only.ts", "u10_reexport.ts", "u10_namespace.ts", "u10_through.ts", "u11_named.ts", "u11_type-only.ts", "u11_reexport.ts", "u11_namespace.ts", "u11_through.ts", "u13_named.ts", "u13_type-only.ts", "u13_reexport.ts", "u13_namespace.ts", "u13_through.ts", "u16_named.ts", "u16_type-only.ts", "u16_reexport.ts", "u16_namespace.ts", "u16_through.ts", "u18_named.ts", "u18_type-only.ts", "u18_reexport.ts", "u18_namespace.ts", "u18_through.ts", "u19_named.ts", "u19_type-only.ts", "u19_reexport.ts", "u19_namespace.ts", "u19_through.ts", "u20_named.ts", "u20_type-only.ts", "u20_reexport.ts", "u20_namespace.ts", "u20_through.ts", "u24_named.ts", "u24_type-only.ts", "u24_reexport.ts", "u24_namespace.ts", "u24_through.ts", "u28_named.ts", "u28_type-only.ts", "u28_reexport.ts", "u28_namespace.ts", "u28_through.ts", "u32_named.ts", "u32_type-only.ts", "u32_reexport.ts", "u32_namespace.ts", "u32_through.ts", "sentinel.ts"]}
// @filename: index.ts
import { Chance } from "lib";

export const a = Chance();
export const b: Chance = a;
// @filename: node_modules/lib-alias-of-alias-property/index.d.ts
interface Instance { n: number }
type Alias = { a: string };
declare class Klass { k: boolean }
declare namespace Inner { interface T { t: 1 } }
declare namespace Outer { namespace Deep { interface D { d: 1 } } }
declare const konst: { c: 1 };
interface Static { (): Instance; new (): Instance; Chance: Static; }
declare var Chance: Static;
declare namespace Chance { export { Instance as Mid }; export { Mid as Chance }; }
export = Chance;
// @filename: node_modules/lib-alias-of-alias-property/package.json
{"name": "lib-alias-of-alias-property", "version": "1.0.0", "types": "index.d.ts"}
// @filename: node_modules/lib-alias-to-class-no-property/index.d.ts
interface Instance { n: number }
type Alias = { a: string };
declare class Klass { k: boolean }
declare namespace Inner { interface T { t: 1 } }
declare namespace Outer { namespace Deep { interface D { d: 1 } } }
declare const konst: { c: 1 };
interface Static { (): Instance; new (): Instance;  }
declare var Chance: Static;
declare namespace Chance { export { Klass as Chance }; }
export = Chance;
// @filename: node_modules/lib-alias-to-class-no-property/package.json
{"name": "lib-alias-to-class-no-property", "version": "1.0.0", "types": "index.d.ts"}
// @filename: node_modules/lib-alias-to-const-property/index.d.ts
interface Instance { n: number }
type Alias = { a: string };
declare class Klass { k: boolean }
declare namespace Inner { interface T { t: 1 } }
declare namespace Outer { namespace Deep { interface D { d: 1 } } }
declare const konst: { c: 1 };
interface Static { (): Instance; new (): Instance; Chance: Static; }
declare var Chance: Static;
declare namespace Chance { export { konst as Chance }; }
export = Chance;
// @filename: node_modules/lib-alias-to-const-property/package.json
{"name": "lib-alias-to-const-property", "version": "1.0.0", "types": "index.d.ts"}
// @filename: node_modules/lib-alias-to-interface-method/index.d.ts
interface Instance { n: number }
type Alias = { a: string };
declare class Klass { k: boolean }
declare namespace Inner { interface T { t: 1 } }
declare namespace Outer { namespace Deep { interface D { d: 1 } } }
declare const konst: { c: 1 };
interface Static { (): Instance; new (): Instance; Chance(): Instance; }
declare var Chance: Static;
declare namespace Chance { export { Instance as Chance }; }
export = Chance;
// @filename: node_modules/lib-alias-to-interface-method/package.json
{"name": "lib-alias-to-interface-method", "version": "1.0.0", "types": "index.d.ts"}
// @filename: node_modules/lib-alias-to-interface-no-property/index.d.ts
interface Instance { n: number }
type Alias = { a: string };
declare class Klass { k: boolean }
declare namespace Inner { interface T { t: 1 } }
declare namespace Outer { namespace Deep { interface D { d: 1 } } }
declare const konst: { c: 1 };
interface Static { (): Instance; new (): Instance;  }
declare var Chance: Static;
declare namespace Chance { export { Instance as Chance }; }
export = Chance;
// @filename: node_modules/lib-alias-to-interface-no-property/package.json
{"name": "lib-alias-to-interface-no-property", "version": "1.0.0", "types": "index.d.ts"}
// @filename: node_modules/lib-alias-to-interface-optional/index.d.ts
interface Instance { n: number }
type Alias = { a: string };
declare class Klass { k: boolean }
declare namespace Inner { interface T { t: 1 } }
declare namespace Outer { namespace Deep { interface D { d: 1 } } }
declare const konst: { c: 1 };
interface Static { (): Instance; new (): Instance; Chance?: Static; }
declare var Chance: Static;
declare namespace Chance { export { Instance as Chance }; }
export = Chance;
// @filename: node_modules/lib-alias-to-interface-optional/package.json
{"name": "lib-alias-to-interface-optional", "version": "1.0.0", "types": "index.d.ts"}
// @filename: node_modules/lib-alias-to-interface-property/index.d.ts
interface Instance { n: number }
type Alias = { a: string };
declare class Klass { k: boolean }
declare namespace Inner { interface T { t: 1 } }
declare namespace Outer { namespace Deep { interface D { d: 1 } } }
declare const konst: { c: 1 };
interface Static { (): Instance; new (): Instance; Chance: Static; }
declare var Chance: Static;
declare namespace Chance { export { Instance as Chance }; }
export = Chance;
// @filename: node_modules/lib-alias-to-interface-property/package.json
{"name": "lib-alias-to-interface-property", "version": "1.0.0", "types": "index.d.ts"}
// @filename: node_modules/lib-alias-to-namespace-method/index.d.ts
interface Instance { n: number }
type Alias = { a: string };
declare class Klass { k: boolean }
declare namespace Inner { interface T { t: 1 } }
declare namespace Outer { namespace Deep { interface D { d: 1 } } }
declare const konst: { c: 1 };
interface Static { (): Instance; new (): Instance; Chance(): Instance; }
declare var Chance: Static;
declare namespace Chance { export { Inner as Chance }; }
export = Chance;
// @filename: node_modules/lib-alias-to-namespace-method/package.json
{"name": "lib-alias-to-namespace-method", "version": "1.0.0", "types": "index.d.ts"}
// @filename: node_modules/lib-alias-to-namespace-optional/index.d.ts
interface Instance { n: number }
type Alias = { a: string };
declare class Klass { k: boolean }
declare namespace Inner { interface T { t: 1 } }
declare namespace Outer { namespace Deep { interface D { d: 1 } } }
declare const konst: { c: 1 };
interface Static { (): Instance; new (): Instance; Chance?: Static; }
declare var Chance: Static;
declare namespace Chance { export { Inner as Chance }; }
export = Chance;
// @filename: node_modules/lib-alias-to-namespace-optional/package.json
{"name": "lib-alias-to-namespace-optional", "version": "1.0.0", "types": "index.d.ts"}
// @filename: node_modules/lib-alias-to-namespace-property/index.d.ts
interface Instance { n: number }
type Alias = { a: string };
declare class Klass { k: boolean }
declare namespace Inner { interface T { t: 1 } }
declare namespace Outer { namespace Deep { interface D { d: 1 } } }
declare const konst: { c: 1 };
interface Static { (): Instance; new (): Instance; Chance: Static; }
declare var Chance: Static;
declare namespace Chance { export { Inner as Chance }; }
export = Chance;
// @filename: node_modules/lib-alias-to-namespace-property/package.json
{"name": "lib-alias-to-namespace-property", "version": "1.0.0", "types": "index.d.ts"}
// @filename: node_modules/lib-alias-to-type-alias-method/index.d.ts
interface Instance { n: number }
type Alias = { a: string };
declare class Klass { k: boolean }
declare namespace Inner { interface T { t: 1 } }
declare namespace Outer { namespace Deep { interface D { d: 1 } } }
declare const konst: { c: 1 };
interface Static { (): Instance; new (): Instance; Chance(): Instance; }
declare var Chance: Static;
declare namespace Chance { export { Alias as Chance }; }
export = Chance;
// @filename: node_modules/lib-alias-to-type-alias-method/package.json
{"name": "lib-alias-to-type-alias-method", "version": "1.0.0", "types": "index.d.ts"}
// @filename: node_modules/lib-alias-to-type-alias-optional/index.d.ts
interface Instance { n: number }
type Alias = { a: string };
declare class Klass { k: boolean }
declare namespace Inner { interface T { t: 1 } }
declare namespace Outer { namespace Deep { interface D { d: 1 } } }
declare const konst: { c: 1 };
interface Static { (): Instance; new (): Instance; Chance?: Static; }
declare var Chance: Static;
declare namespace Chance { export { Alias as Chance }; }
export = Chance;
// @filename: node_modules/lib-alias-to-type-alias-optional/package.json
{"name": "lib-alias-to-type-alias-optional", "version": "1.0.0", "types": "index.d.ts"}
// @filename: node_modules/lib-alias-to-type-alias-property/index.d.ts
interface Instance { n: number }
type Alias = { a: string };
declare class Klass { k: boolean }
declare namespace Inner { interface T { t: 1 } }
declare namespace Outer { namespace Deep { interface D { d: 1 } } }
declare const konst: { c: 1 };
interface Static { (): Instance; new (): Instance; Chance: Static; }
declare var Chance: Static;
declare namespace Chance { export { Alias as Chance }; }
export = Chance;
// @filename: node_modules/lib-alias-to-type-alias-property/package.json
{"name": "lib-alias-to-type-alias-property", "version": "1.0.0", "types": "index.d.ts"}
// @filename: node_modules/lib-direct-interface-method/index.d.ts
interface Instance { n: number }
type Alias = { a: string };
declare class Klass { k: boolean }
declare namespace Inner { interface T { t: 1 } }
declare namespace Outer { namespace Deep { interface D { d: 1 } } }
declare const konst: { c: 1 };
interface Static { (): Instance; new (): Instance; Chance(): Instance; }
declare var Chance: Static;
declare namespace Chance { export interface Chance { n: number } }
export = Chance;
// @filename: node_modules/lib-direct-interface-method/package.json
{"name": "lib-direct-interface-method", "version": "1.0.0", "types": "index.d.ts"}
// @filename: node_modules/lib-direct-interface-optional/index.d.ts
interface Instance { n: number }
type Alias = { a: string };
declare class Klass { k: boolean }
declare namespace Inner { interface T { t: 1 } }
declare namespace Outer { namespace Deep { interface D { d: 1 } } }
declare const konst: { c: 1 };
interface Static { (): Instance; new (): Instance; Chance?: Static; }
declare var Chance: Static;
declare namespace Chance { export interface Chance { n: number } }
export = Chance;
// @filename: node_modules/lib-direct-interface-optional/package.json
{"name": "lib-direct-interface-optional", "version": "1.0.0", "types": "index.d.ts"}
// @filename: node_modules/lib-direct-interface-property/index.d.ts
interface Instance { n: number }
type Alias = { a: string };
declare class Klass { k: boolean }
declare namespace Inner { interface T { t: 1 } }
declare namespace Outer { namespace Deep { interface D { d: 1 } } }
declare const konst: { c: 1 };
interface Static { (): Instance; new (): Instance; Chance: Static; }
declare var Chance: Static;
declare namespace Chance { export interface Chance { n: number } }
export = Chance;
// @filename: node_modules/lib-direct-interface-property/package.json
{"name": "lib-direct-interface-property", "version": "1.0.0", "types": "index.d.ts"}
// @filename: node_modules/lib-import-equals-property/index.d.ts
interface Instance { n: number }
type Alias = { a: string };
declare class Klass { k: boolean }
declare namespace Inner { interface T { t: 1 } }
declare namespace Outer { namespace Deep { interface D { d: 1 } } }
declare const konst: { c: 1 };
interface Static { (): Instance; new (): Instance; Chance: Static; }
declare var Chance: Static;
declare namespace Chance { export import Chance = Outer.Deep; }
export = Chance;
// @filename: node_modules/lib-import-equals-property/package.json
{"name": "lib-import-equals-property", "version": "1.0.0", "types": "index.d.ts"}
// @filename: node_modules/lib-nothing-property/index.d.ts
interface Instance { n: number }
type Alias = { a: string };
declare class Klass { k: boolean }
declare namespace Inner { interface T { t: 1 } }
declare namespace Outer { namespace Deep { interface D { d: 1 } } }
declare const konst: { c: 1 };
interface Static { (): Instance; new (): Instance; Chance: Static; }
declare var Chance: Static;
declare namespace Chance { export interface Other { o: 1 } }
export = Chance;
// @filename: node_modules/lib-nothing-property/package.json
{"name": "lib-nothing-property", "version": "1.0.0", "types": "index.d.ts"}
// @filename: node_modules/lib/index.d.ts
interface Instance {
  n: number;
}
interface Static {
  (): Instance;
  new (): Instance;
  Chance: Static;
}
declare var Chance: Static;
declare namespace Chance {
  export { Instance as Chance };
}
export = Chance;
// @filename: node_modules/lib/package.json
{ "name": "lib", "version": "1.0.0", "types": "index.d.ts" }
// @filename: sentinel.ts
const sentinel: number = "";
export {};
// @filename: u0_named.ts
import { Chance } from "lib-alias-to-interface-property";
const v: never = Chance;
const t: never = null! as Chance;
export {};
// @filename: u0_namespace.ts
import * as ns from "lib-alias-to-interface-property";
const v: never = ns.Chance;
const t: never = null! as ns.Chance;
export {};
// @filename: u0_reexport.ts
export { Chance } from "lib-alias-to-interface-property";
export {};
// @filename: u0_through.ts
import { Chance } from "./u0_reexport";
const v: never = Chance;
const t: never = null! as Chance;
export {};
// @filename: u0_type-only.ts
import type { Chance } from "lib-alias-to-interface-property";
const t: never = null! as Chance;
const v: never = Chance;
export {};
// @filename: u10_named.ts
import { Chance } from "lib-alias-to-type-alias-optional";
const v: never = Chance;
const t: never = null! as Chance;
export {};
// @filename: u10_namespace.ts
import * as ns from "lib-alias-to-type-alias-optional";
const v: never = ns.Chance;
const t: never = null! as ns.Chance;
export {};
// @filename: u10_reexport.ts
export { Chance } from "lib-alias-to-type-alias-optional";
export {};
// @filename: u10_through.ts
import { Chance } from "./u10_reexport";
const v: never = Chance;
const t: never = null! as Chance;
export {};
// @filename: u10_type-only.ts
import type { Chance } from "lib-alias-to-type-alias-optional";
const t: never = null! as Chance;
const v: never = Chance;
export {};
// @filename: u11_named.ts
import { Chance } from "lib-alias-to-type-alias-method";
const v: never = Chance;
const t: never = null! as Chance;
export {};
// @filename: u11_namespace.ts
import * as ns from "lib-alias-to-type-alias-method";
const v: never = ns.Chance;
const t: never = null! as ns.Chance;
export {};
// @filename: u11_reexport.ts
export { Chance } from "lib-alias-to-type-alias-method";
export {};
// @filename: u11_through.ts
import { Chance } from "./u11_reexport";
const v: never = Chance;
const t: never = null! as Chance;
export {};
// @filename: u11_type-only.ts
import type { Chance } from "lib-alias-to-type-alias-method";
const t: never = null! as Chance;
const v: never = Chance;
export {};
// @filename: u13_named.ts
import { Chance } from "lib-alias-to-class-no-property";
const v: never = Chance;
const t: never = null! as Chance;
export {};
// @filename: u13_namespace.ts
import * as ns from "lib-alias-to-class-no-property";
const v: never = ns.Chance;
const t: never = null! as ns.Chance;
export {};
// @filename: u13_reexport.ts
export { Chance } from "lib-alias-to-class-no-property";
export {};
// @filename: u13_through.ts
import { Chance } from "./u13_reexport";
const v: never = Chance;
const t: never = null! as Chance;
export {};
// @filename: u13_type-only.ts
import type { Chance } from "lib-alias-to-class-no-property";
const t: never = null! as Chance;
const v: never = Chance;
export {};
// @filename: u16_named.ts
import { Chance } from "lib-alias-to-namespace-property";
const v: never = Chance;
const t: never = null! as Chance;
export {};
// @filename: u16_namespace.ts
import * as ns from "lib-alias-to-namespace-property";
const v: never = ns.Chance;
const t: never = null! as ns.Chance;
export {};
// @filename: u16_reexport.ts
export { Chance } from "lib-alias-to-namespace-property";
export {};
// @filename: u16_through.ts
import { Chance } from "./u16_reexport";
const v: never = Chance;
const t: never = null! as Chance;
export {};
// @filename: u16_type-only.ts
import type { Chance } from "lib-alias-to-namespace-property";
const t: never = null! as Chance;
const v: never = Chance;
export {};
// @filename: u18_named.ts
import { Chance } from "lib-alias-to-namespace-optional";
const v: never = Chance;
const t: never = null! as Chance;
export {};
// @filename: u18_namespace.ts
import * as ns from "lib-alias-to-namespace-optional";
const v: never = ns.Chance;
const t: never = null! as ns.Chance;
export {};
// @filename: u18_reexport.ts
export { Chance } from "lib-alias-to-namespace-optional";
export {};
// @filename: u18_through.ts
import { Chance } from "./u18_reexport";
const v: never = Chance;
const t: never = null! as Chance;
export {};
// @filename: u18_type-only.ts
import type { Chance } from "lib-alias-to-namespace-optional";
const t: never = null! as Chance;
const v: never = Chance;
export {};
// @filename: u19_named.ts
import { Chance } from "lib-alias-to-namespace-method";
const v: never = Chance;
const t: never = null! as Chance;
export {};
// @filename: u19_namespace.ts
import * as ns from "lib-alias-to-namespace-method";
const v: never = ns.Chance;
const t: never = null! as ns.Chance;
export {};
// @filename: u19_reexport.ts
export { Chance } from "lib-alias-to-namespace-method";
export {};
// @filename: u19_through.ts
import { Chance } from "./u19_reexport";
const v: never = Chance;
const t: never = null! as Chance;
export {};
// @filename: u19_type-only.ts
import type { Chance } from "lib-alias-to-namespace-method";
const t: never = null! as Chance;
const v: never = Chance;
export {};
// @filename: u1_named.ts
import { Chance } from "lib-alias-to-interface-no-property";
const v: never = Chance;
const t: never = null! as Chance;
export {};
// @filename: u1_namespace.ts
import * as ns from "lib-alias-to-interface-no-property";
const v: never = ns.Chance;
const t: never = null! as ns.Chance;
export {};
// @filename: u1_reexport.ts
export { Chance } from "lib-alias-to-interface-no-property";
export {};
// @filename: u1_through.ts
import { Chance } from "./u1_reexport";
const v: never = Chance;
const t: never = null! as Chance;
export {};
// @filename: u1_type-only.ts
import type { Chance } from "lib-alias-to-interface-no-property";
const t: never = null! as Chance;
const v: never = Chance;
export {};
// @filename: u20_named.ts
import { Chance } from "lib-alias-to-const-property";
const v: never = Chance;
const t: never = null! as Chance;
export {};
// @filename: u20_namespace.ts
import * as ns from "lib-alias-to-const-property";
const v: never = ns.Chance;
const t: never = null! as ns.Chance;
export {};
// @filename: u20_reexport.ts
export { Chance } from "lib-alias-to-const-property";
export {};
// @filename: u20_through.ts
import { Chance } from "./u20_reexport";
const v: never = Chance;
const t: never = null! as Chance;
export {};
// @filename: u20_type-only.ts
import type { Chance } from "lib-alias-to-const-property";
const t: never = null! as Chance;
const v: never = Chance;
export {};
// @filename: u24_named.ts
import { Chance } from "lib-alias-of-alias-property";
const v: never = Chance;
const t: never = null! as Chance;
export {};
// @filename: u24_namespace.ts
import * as ns from "lib-alias-of-alias-property";
const v: never = ns.Chance;
const t: never = null! as ns.Chance;
export {};
// @filename: u24_reexport.ts
export { Chance } from "lib-alias-of-alias-property";
export {};
// @filename: u24_through.ts
import { Chance } from "./u24_reexport";
const v: never = Chance;
const t: never = null! as Chance;
export {};
// @filename: u24_type-only.ts
import type { Chance } from "lib-alias-of-alias-property";
const t: never = null! as Chance;
const v: never = Chance;
export {};
// @filename: u28_named.ts
import { Chance } from "lib-import-equals-property";
const v: never = Chance;
const t: never = null! as Chance;
export {};
// @filename: u28_namespace.ts
import * as ns from "lib-import-equals-property";
const v: never = ns.Chance;
const t: never = null! as ns.Chance;
export {};
// @filename: u28_reexport.ts
export { Chance } from "lib-import-equals-property";
export {};
// @filename: u28_through.ts
import { Chance } from "./u28_reexport";
const v: never = Chance;
const t: never = null! as Chance;
export {};
// @filename: u28_type-only.ts
import type { Chance } from "lib-import-equals-property";
const t: never = null! as Chance;
const v: never = Chance;
export {};
// @filename: u2_named.ts
import { Chance } from "lib-alias-to-interface-optional";
const v: never = Chance;
const t: never = null! as Chance;
export {};
// @filename: u2_namespace.ts
import * as ns from "lib-alias-to-interface-optional";
const v: never = ns.Chance;
const t: never = null! as ns.Chance;
export {};
// @filename: u2_reexport.ts
export { Chance } from "lib-alias-to-interface-optional";
export {};
// @filename: u2_through.ts
import { Chance } from "./u2_reexport";
const v: never = Chance;
const t: never = null! as Chance;
export {};
// @filename: u2_type-only.ts
import type { Chance } from "lib-alias-to-interface-optional";
const t: never = null! as Chance;
const v: never = Chance;
export {};
// @filename: u32_named.ts
import { Chance } from "lib-nothing-property";
const v: never = Chance;
const t: never = null! as Chance;
export {};
// @filename: u32_namespace.ts
import * as ns from "lib-nothing-property";
const v: never = ns.Chance;
const t: never = null! as ns.Chance;
export {};
// @filename: u32_reexport.ts
export { Chance } from "lib-nothing-property";
export {};
// @filename: u32_through.ts
import { Chance } from "./u32_reexport";
const v: never = Chance;
const t: never = null! as Chance;
export {};
// @filename: u32_type-only.ts
import type { Chance } from "lib-nothing-property";
const t: never = null! as Chance;
const v: never = Chance;
export {};
// @filename: u3_named.ts
import { Chance } from "lib-alias-to-interface-method";
const v: never = Chance;
const t: never = null! as Chance;
export {};
// @filename: u3_namespace.ts
import * as ns from "lib-alias-to-interface-method";
const v: never = ns.Chance;
const t: never = null! as ns.Chance;
export {};
// @filename: u3_reexport.ts
export { Chance } from "lib-alias-to-interface-method";
export {};
// @filename: u3_through.ts
import { Chance } from "./u3_reexport";
const v: never = Chance;
const t: never = null! as Chance;
export {};
// @filename: u3_type-only.ts
import type { Chance } from "lib-alias-to-interface-method";
const t: never = null! as Chance;
const v: never = Chance;
export {};
// @filename: u4_named.ts
import { Chance } from "lib-direct-interface-property";
const v: never = Chance;
const t: never = null! as Chance;
export {};
// @filename: u4_namespace.ts
import * as ns from "lib-direct-interface-property";
const v: never = ns.Chance;
const t: never = null! as ns.Chance;
export {};
// @filename: u4_reexport.ts
export { Chance } from "lib-direct-interface-property";
export {};
// @filename: u4_through.ts
import { Chance } from "./u4_reexport";
const v: never = Chance;
const t: never = null! as Chance;
export {};
// @filename: u4_type-only.ts
import type { Chance } from "lib-direct-interface-property";
const t: never = null! as Chance;
const v: never = Chance;
export {};
// @filename: u6_named.ts
import { Chance } from "lib-direct-interface-optional";
const v: never = Chance;
const t: never = null! as Chance;
export {};
// @filename: u6_namespace.ts
import * as ns from "lib-direct-interface-optional";
const v: never = ns.Chance;
const t: never = null! as ns.Chance;
export {};
// @filename: u6_reexport.ts
export { Chance } from "lib-direct-interface-optional";
export {};
// @filename: u6_through.ts
import { Chance } from "./u6_reexport";
const v: never = Chance;
const t: never = null! as Chance;
export {};
// @filename: u6_type-only.ts
import type { Chance } from "lib-direct-interface-optional";
const t: never = null! as Chance;
const v: never = Chance;
export {};
// @filename: u7_named.ts
import { Chance } from "lib-direct-interface-method";
const v: never = Chance;
const t: never = null! as Chance;
export {};
// @filename: u7_namespace.ts
import * as ns from "lib-direct-interface-method";
const v: never = ns.Chance;
const t: never = null! as ns.Chance;
export {};
// @filename: u7_reexport.ts
export { Chance } from "lib-direct-interface-method";
export {};
// @filename: u7_through.ts
import { Chance } from "./u7_reexport";
const v: never = Chance;
const t: never = null! as Chance;
export {};
// @filename: u7_type-only.ts
import type { Chance } from "lib-direct-interface-method";
const t: never = null! as Chance;
const v: never = Chance;
export {};
// @filename: u8_named.ts
import { Chance } from "lib-alias-to-type-alias-property";
const v: never = Chance;
const t: never = null! as Chance;
export {};
// @filename: u8_namespace.ts
import * as ns from "lib-alias-to-type-alias-property";
const v: never = ns.Chance;
const t: never = null! as ns.Chance;
export {};
// @filename: u8_reexport.ts
export { Chance } from "lib-alias-to-type-alias-property";
export {};
// @filename: u8_through.ts
import { Chance } from "./u8_reexport";
const v: never = Chance;
const t: never = null! as Chance;
export {};
// @filename: u8_type-only.ts
import type { Chance } from "lib-alias-to-type-alias-property";
const t: never = null! as Chance;
const v: never = Chance;
export {};
