// @noTypesAndSymbols: true
// @filename: tsconfig.json
{"compilerOptions": {"strict": true, "noEmit": true, "target": "esnext", "module": "esnext", "moduleResolution": "bundler", "types": [], "skipLibCheck": true}, "files": ["u_class_call.ts", "u_class_call_new.ts", "u_class_prop_other.ts", "u_class_optional.ts", "u_class_method.ts", "u_class_members_call.ts", "u_class_members_call_new.ts", "u_class_members_prop_other.ts", "u_class_members_optional.ts", "u_class_members_method.ts", "u_const_call.ts", "u_const_call_new.ts", "u_const_prop_other.ts", "u_const_optional.ts", "u_const_method.ts", "u_function_call.ts", "u_function_call_new.ts", "u_function_prop_other.ts", "u_function_optional.ts", "u_function_method.ts", "u_enum_call.ts", "u_enum_call_new.ts", "u_enum_prop_other.ts", "u_enum_optional.ts", "u_enum_method.ts", "u_namespace_value_call.ts", "u_namespace_value_call_new.ts", "u_namespace_value_prop_other.ts", "u_namespace_value_optional.ts", "u_namespace_value_method.ts", "u_class_and_interface_call.ts", "u_class_and_interface_call_new.ts", "u_class_and_interface_prop_other.ts", "u_class_and_interface_optional.ts", "u_class_and_interface_method.ts", "sentinel.ts"]}
// @filename: node_modules/lib-class-and-interface-call-new/index.d.ts
interface Instance { n: number }
interface Static { (): Instance; new (): Instance; Chance: Static }
declare var Chance: Static;
declare class Klass {}
interface Klass { i: 1 }
declare namespace Chance { export { Klass as Chance }; }
export = Chance;
// @filename: node_modules/lib-class-and-interface-call-new/package.json
{"name": "lib-class-and-interface-call-new", "version": "1.0.0", "types": "index.d.ts"}
// @filename: node_modules/lib-class-and-interface-call/index.d.ts
interface Instance { n: number }
interface Static { (): Instance; Chance: Static }
declare var Chance: Static;
declare class Klass {}
interface Klass { i: 1 }
declare namespace Chance { export { Klass as Chance }; }
export = Chance;
// @filename: node_modules/lib-class-and-interface-call/package.json
{"name": "lib-class-and-interface-call", "version": "1.0.0", "types": "index.d.ts"}
// @filename: node_modules/lib-class-and-interface-method/index.d.ts
interface Instance { n: number }
interface Static { (): Instance; Chance(): "m" }
declare var Chance: Static;
declare class Klass {}
interface Klass { i: 1 }
declare namespace Chance { export { Klass as Chance }; }
export = Chance;
// @filename: node_modules/lib-class-and-interface-method/package.json
{"name": "lib-class-and-interface-method", "version": "1.0.0", "types": "index.d.ts"}
// @filename: node_modules/lib-class-and-interface-optional/index.d.ts
interface Instance { n: number }
interface Static { (): Instance; Chance?: Static }
declare var Chance: Static;
declare class Klass {}
interface Klass { i: 1 }
declare namespace Chance { export { Klass as Chance }; }
export = Chance;
// @filename: node_modules/lib-class-and-interface-optional/package.json
{"name": "lib-class-and-interface-optional", "version": "1.0.0", "types": "index.d.ts"}
// @filename: node_modules/lib-class-and-interface-prop-other/index.d.ts
interface Instance { n: number }
interface Static { (): Instance; Chance: { other: 1 } }
declare var Chance: Static;
declare class Klass {}
interface Klass { i: 1 }
declare namespace Chance { export { Klass as Chance }; }
export = Chance;
// @filename: node_modules/lib-class-and-interface-prop-other/package.json
{"name": "lib-class-and-interface-prop-other", "version": "1.0.0", "types": "index.d.ts"}
// @filename: node_modules/lib-class-call-new/index.d.ts
interface Instance { n: number }
interface Static { (): Instance; new (): Instance; Chance: Static }
declare var Chance: Static;
declare class Klass {}
declare namespace Chance { export { Klass as Chance }; }
export = Chance;
// @filename: node_modules/lib-class-call-new/package.json
{"name": "lib-class-call-new", "version": "1.0.0", "types": "index.d.ts"}
// @filename: node_modules/lib-class-call/index.d.ts
interface Instance { n: number }
interface Static { (): Instance; Chance: Static }
declare var Chance: Static;
declare class Klass {}
declare namespace Chance { export { Klass as Chance }; }
export = Chance;
// @filename: node_modules/lib-class-call/package.json
{"name": "lib-class-call", "version": "1.0.0", "types": "index.d.ts"}
// @filename: node_modules/lib-class-members-call-new/index.d.ts
interface Instance { n: number }
interface Static { (): Instance; new (): Instance; Chance: Static }
declare var Chance: Static;
declare class Klass { k: string; static s: number }
declare namespace Chance { export { Klass as Chance }; }
export = Chance;
// @filename: node_modules/lib-class-members-call-new/package.json
{"name": "lib-class-members-call-new", "version": "1.0.0", "types": "index.d.ts"}
// @filename: node_modules/lib-class-members-call/index.d.ts
interface Instance { n: number }
interface Static { (): Instance; Chance: Static }
declare var Chance: Static;
declare class Klass { k: string; static s: number }
declare namespace Chance { export { Klass as Chance }; }
export = Chance;
// @filename: node_modules/lib-class-members-call/package.json
{"name": "lib-class-members-call", "version": "1.0.0", "types": "index.d.ts"}
// @filename: node_modules/lib-class-members-method/index.d.ts
interface Instance { n: number }
interface Static { (): Instance; Chance(): "m" }
declare var Chance: Static;
declare class Klass { k: string; static s: number }
declare namespace Chance { export { Klass as Chance }; }
export = Chance;
// @filename: node_modules/lib-class-members-method/package.json
{"name": "lib-class-members-method", "version": "1.0.0", "types": "index.d.ts"}
// @filename: node_modules/lib-class-members-optional/index.d.ts
interface Instance { n: number }
interface Static { (): Instance; Chance?: Static }
declare var Chance: Static;
declare class Klass { k: string; static s: number }
declare namespace Chance { export { Klass as Chance }; }
export = Chance;
// @filename: node_modules/lib-class-members-optional/package.json
{"name": "lib-class-members-optional", "version": "1.0.0", "types": "index.d.ts"}
// @filename: node_modules/lib-class-members-prop-other/index.d.ts
interface Instance { n: number }
interface Static { (): Instance; Chance: { other: 1 } }
declare var Chance: Static;
declare class Klass { k: string; static s: number }
declare namespace Chance { export { Klass as Chance }; }
export = Chance;
// @filename: node_modules/lib-class-members-prop-other/package.json
{"name": "lib-class-members-prop-other", "version": "1.0.0", "types": "index.d.ts"}
// @filename: node_modules/lib-class-method/index.d.ts
interface Instance { n: number }
interface Static { (): Instance; Chance(): "m" }
declare var Chance: Static;
declare class Klass {}
declare namespace Chance { export { Klass as Chance }; }
export = Chance;
// @filename: node_modules/lib-class-method/package.json
{"name": "lib-class-method", "version": "1.0.0", "types": "index.d.ts"}
// @filename: node_modules/lib-class-optional/index.d.ts
interface Instance { n: number }
interface Static { (): Instance; Chance?: Static }
declare var Chance: Static;
declare class Klass {}
declare namespace Chance { export { Klass as Chance }; }
export = Chance;
// @filename: node_modules/lib-class-optional/package.json
{"name": "lib-class-optional", "version": "1.0.0", "types": "index.d.ts"}
// @filename: node_modules/lib-class-prop-other/index.d.ts
interface Instance { n: number }
interface Static { (): Instance; Chance: { other: 1 } }
declare var Chance: Static;
declare class Klass {}
declare namespace Chance { export { Klass as Chance }; }
export = Chance;
// @filename: node_modules/lib-class-prop-other/package.json
{"name": "lib-class-prop-other", "version": "1.0.0", "types": "index.d.ts"}
// @filename: node_modules/lib-const-call-new/index.d.ts
interface Instance { n: number }
interface Static { (): Instance; new (): Instance; Chance: Static }
declare var Chance: Static;
declare const Klass: { c: 1 };
declare namespace Chance { export { Klass as Chance }; }
export = Chance;
// @filename: node_modules/lib-const-call-new/package.json
{"name": "lib-const-call-new", "version": "1.0.0", "types": "index.d.ts"}
// @filename: node_modules/lib-const-call/index.d.ts
interface Instance { n: number }
interface Static { (): Instance; Chance: Static }
declare var Chance: Static;
declare const Klass: { c: 1 };
declare namespace Chance { export { Klass as Chance }; }
export = Chance;
// @filename: node_modules/lib-const-call/package.json
{"name": "lib-const-call", "version": "1.0.0", "types": "index.d.ts"}
// @filename: node_modules/lib-const-method/index.d.ts
interface Instance { n: number }
interface Static { (): Instance; Chance(): "m" }
declare var Chance: Static;
declare const Klass: { c: 1 };
declare namespace Chance { export { Klass as Chance }; }
export = Chance;
// @filename: node_modules/lib-const-method/package.json
{"name": "lib-const-method", "version": "1.0.0", "types": "index.d.ts"}
// @filename: node_modules/lib-const-optional/index.d.ts
interface Instance { n: number }
interface Static { (): Instance; Chance?: Static }
declare var Chance: Static;
declare const Klass: { c: 1 };
declare namespace Chance { export { Klass as Chance }; }
export = Chance;
// @filename: node_modules/lib-const-optional/package.json
{"name": "lib-const-optional", "version": "1.0.0", "types": "index.d.ts"}
// @filename: node_modules/lib-const-prop-other/index.d.ts
interface Instance { n: number }
interface Static { (): Instance; Chance: { other: 1 } }
declare var Chance: Static;
declare const Klass: { c: 1 };
declare namespace Chance { export { Klass as Chance }; }
export = Chance;
// @filename: node_modules/lib-const-prop-other/package.json
{"name": "lib-const-prop-other", "version": "1.0.0", "types": "index.d.ts"}
// @filename: node_modules/lib-enum-call-new/index.d.ts
interface Instance { n: number }
interface Static { (): Instance; new (): Instance; Chance: Static }
declare var Chance: Static;
declare enum Klass { A }
declare namespace Chance { export { Klass as Chance }; }
export = Chance;
// @filename: node_modules/lib-enum-call-new/package.json
{"name": "lib-enum-call-new", "version": "1.0.0", "types": "index.d.ts"}
// @filename: node_modules/lib-enum-call/index.d.ts
interface Instance { n: number }
interface Static { (): Instance; Chance: Static }
declare var Chance: Static;
declare enum Klass { A }
declare namespace Chance { export { Klass as Chance }; }
export = Chance;
// @filename: node_modules/lib-enum-call/package.json
{"name": "lib-enum-call", "version": "1.0.0", "types": "index.d.ts"}
// @filename: node_modules/lib-enum-method/index.d.ts
interface Instance { n: number }
interface Static { (): Instance; Chance(): "m" }
declare var Chance: Static;
declare enum Klass { A }
declare namespace Chance { export { Klass as Chance }; }
export = Chance;
// @filename: node_modules/lib-enum-method/package.json
{"name": "lib-enum-method", "version": "1.0.0", "types": "index.d.ts"}
// @filename: node_modules/lib-enum-optional/index.d.ts
interface Instance { n: number }
interface Static { (): Instance; Chance?: Static }
declare var Chance: Static;
declare enum Klass { A }
declare namespace Chance { export { Klass as Chance }; }
export = Chance;
// @filename: node_modules/lib-enum-optional/package.json
{"name": "lib-enum-optional", "version": "1.0.0", "types": "index.d.ts"}
// @filename: node_modules/lib-enum-prop-other/index.d.ts
interface Instance { n: number }
interface Static { (): Instance; Chance: { other: 1 } }
declare var Chance: Static;
declare enum Klass { A }
declare namespace Chance { export { Klass as Chance }; }
export = Chance;
// @filename: node_modules/lib-enum-prop-other/package.json
{"name": "lib-enum-prop-other", "version": "1.0.0", "types": "index.d.ts"}
// @filename: node_modules/lib-function-call-new/index.d.ts
interface Instance { n: number }
interface Static { (): Instance; new (): Instance; Chance: Static }
declare var Chance: Static;
declare function Klass(): "f";
declare namespace Chance { export { Klass as Chance }; }
export = Chance;
// @filename: node_modules/lib-function-call-new/package.json
{"name": "lib-function-call-new", "version": "1.0.0", "types": "index.d.ts"}
// @filename: node_modules/lib-function-call/index.d.ts
interface Instance { n: number }
interface Static { (): Instance; Chance: Static }
declare var Chance: Static;
declare function Klass(): "f";
declare namespace Chance { export { Klass as Chance }; }
export = Chance;
// @filename: node_modules/lib-function-call/package.json
{"name": "lib-function-call", "version": "1.0.0", "types": "index.d.ts"}
// @filename: node_modules/lib-function-method/index.d.ts
interface Instance { n: number }
interface Static { (): Instance; Chance(): "m" }
declare var Chance: Static;
declare function Klass(): "f";
declare namespace Chance { export { Klass as Chance }; }
export = Chance;
// @filename: node_modules/lib-function-method/package.json
{"name": "lib-function-method", "version": "1.0.0", "types": "index.d.ts"}
// @filename: node_modules/lib-function-optional/index.d.ts
interface Instance { n: number }
interface Static { (): Instance; Chance?: Static }
declare var Chance: Static;
declare function Klass(): "f";
declare namespace Chance { export { Klass as Chance }; }
export = Chance;
// @filename: node_modules/lib-function-optional/package.json
{"name": "lib-function-optional", "version": "1.0.0", "types": "index.d.ts"}
// @filename: node_modules/lib-function-prop-other/index.d.ts
interface Instance { n: number }
interface Static { (): Instance; Chance: { other: 1 } }
declare var Chance: Static;
declare function Klass(): "f";
declare namespace Chance { export { Klass as Chance }; }
export = Chance;
// @filename: node_modules/lib-function-prop-other/package.json
{"name": "lib-function-prop-other", "version": "1.0.0", "types": "index.d.ts"}
// @filename: node_modules/lib-namespace-value-call-new/index.d.ts
interface Instance { n: number }
interface Static { (): Instance; new (): Instance; Chance: Static }
declare var Chance: Static;
declare namespace Klass { const v: 1 }
declare namespace Chance { export { Klass as Chance }; }
export = Chance;
// @filename: node_modules/lib-namespace-value-call-new/package.json
{"name": "lib-namespace-value-call-new", "version": "1.0.0", "types": "index.d.ts"}
// @filename: node_modules/lib-namespace-value-call/index.d.ts
interface Instance { n: number }
interface Static { (): Instance; Chance: Static }
declare var Chance: Static;
declare namespace Klass { const v: 1 }
declare namespace Chance { export { Klass as Chance }; }
export = Chance;
// @filename: node_modules/lib-namespace-value-call/package.json
{"name": "lib-namespace-value-call", "version": "1.0.0", "types": "index.d.ts"}
// @filename: node_modules/lib-namespace-value-method/index.d.ts
interface Instance { n: number }
interface Static { (): Instance; Chance(): "m" }
declare var Chance: Static;
declare namespace Klass { const v: 1 }
declare namespace Chance { export { Klass as Chance }; }
export = Chance;
// @filename: node_modules/lib-namespace-value-method/package.json
{"name": "lib-namespace-value-method", "version": "1.0.0", "types": "index.d.ts"}
// @filename: node_modules/lib-namespace-value-optional/index.d.ts
interface Instance { n: number }
interface Static { (): Instance; Chance?: Static }
declare var Chance: Static;
declare namespace Klass { const v: 1 }
declare namespace Chance { export { Klass as Chance }; }
export = Chance;
// @filename: node_modules/lib-namespace-value-optional/package.json
{"name": "lib-namespace-value-optional", "version": "1.0.0", "types": "index.d.ts"}
// @filename: node_modules/lib-namespace-value-prop-other/index.d.ts
interface Instance { n: number }
interface Static { (): Instance; Chance: { other: 1 } }
declare var Chance: Static;
declare namespace Klass { const v: 1 }
declare namespace Chance { export { Klass as Chance }; }
export = Chance;
// @filename: node_modules/lib-namespace-value-prop-other/package.json
{"name": "lib-namespace-value-prop-other", "version": "1.0.0", "types": "index.d.ts"}
// @filename: sentinel.ts
export const sentinel: number = "";
// @filename: u_class_and_interface_call.ts
import { Chance } from "lib-class-and-interface-call";
const a = Chance();
const p1: never = Chance;
const p2: never = a;
let t: Chance = null!;
const p3: never = t;
export {};
// @filename: u_class_and_interface_call_new.ts
import { Chance } from "lib-class-and-interface-call-new";
const a = Chance();
const p1: never = Chance;
const p2: never = a;
let t: Chance = null!;
const p3: never = t;
export {};
// @filename: u_class_and_interface_method.ts
import { Chance } from "lib-class-and-interface-method";
const a = Chance();
const p1: never = Chance;
const p2: never = a;
let t: Chance = null!;
const p3: never = t;
export {};
// @filename: u_class_and_interface_optional.ts
import { Chance } from "lib-class-and-interface-optional";
const a = Chance();
const p1: never = Chance;
const p2: never = a;
let t: Chance = null!;
const p3: never = t;
export {};
// @filename: u_class_and_interface_prop_other.ts
import { Chance } from "lib-class-and-interface-prop-other";
const a = Chance();
const p1: never = Chance;
const p2: never = a;
let t: Chance = null!;
const p3: never = t;
export {};
// @filename: u_class_call.ts
import { Chance } from "lib-class-call";
const a = Chance();
const p1: never = Chance;
const p2: never = a;
let t: Chance = null!;
const p3: never = t;
export {};
// @filename: u_class_call_new.ts
import { Chance } from "lib-class-call-new";
const a = Chance();
const p1: never = Chance;
const p2: never = a;
let t: Chance = null!;
const p3: never = t;
export {};
// @filename: u_class_members_call.ts
import { Chance } from "lib-class-members-call";
const a = Chance();
const p1: never = Chance;
const p2: never = a;
let t: Chance = null!;
const p3: never = t;
export {};
// @filename: u_class_members_call_new.ts
import { Chance } from "lib-class-members-call-new";
const a = Chance();
const p1: never = Chance;
const p2: never = a;
let t: Chance = null!;
const p3: never = t;
export {};
// @filename: u_class_members_method.ts
import { Chance } from "lib-class-members-method";
const a = Chance();
const p1: never = Chance;
const p2: never = a;
let t: Chance = null!;
const p3: never = t;
export {};
// @filename: u_class_members_optional.ts
import { Chance } from "lib-class-members-optional";
const a = Chance();
const p1: never = Chance;
const p2: never = a;
let t: Chance = null!;
const p3: never = t;
export {};
// @filename: u_class_members_prop_other.ts
import { Chance } from "lib-class-members-prop-other";
const a = Chance();
const p1: never = Chance;
const p2: never = a;
let t: Chance = null!;
const p3: never = t;
export {};
// @filename: u_class_method.ts
import { Chance } from "lib-class-method";
const a = Chance();
const p1: never = Chance;
const p2: never = a;
let t: Chance = null!;
const p3: never = t;
export {};
// @filename: u_class_optional.ts
import { Chance } from "lib-class-optional";
const a = Chance();
const p1: never = Chance;
const p2: never = a;
let t: Chance = null!;
const p3: never = t;
export {};
// @filename: u_class_prop_other.ts
import { Chance } from "lib-class-prop-other";
const a = Chance();
const p1: never = Chance;
const p2: never = a;
let t: Chance = null!;
const p3: never = t;
export {};
// @filename: u_const_call.ts
import { Chance } from "lib-const-call";
const a = Chance();
const p1: never = Chance;
const p2: never = a;
let t: Chance = null!;
const p3: never = t;
export {};
// @filename: u_const_call_new.ts
import { Chance } from "lib-const-call-new";
const a = Chance();
const p1: never = Chance;
const p2: never = a;
let t: Chance = null!;
const p3: never = t;
export {};
// @filename: u_const_method.ts
import { Chance } from "lib-const-method";
const a = Chance();
const p1: never = Chance;
const p2: never = a;
let t: Chance = null!;
const p3: never = t;
export {};
// @filename: u_const_optional.ts
import { Chance } from "lib-const-optional";
const a = Chance();
const p1: never = Chance;
const p2: never = a;
let t: Chance = null!;
const p3: never = t;
export {};
// @filename: u_const_prop_other.ts
import { Chance } from "lib-const-prop-other";
const a = Chance();
const p1: never = Chance;
const p2: never = a;
let t: Chance = null!;
const p3: never = t;
export {};
// @filename: u_enum_call.ts
import { Chance } from "lib-enum-call";
const a = Chance();
const p1: never = Chance;
const p2: never = a;
let t: Chance = null!;
const p3: never = t;
export {};
// @filename: u_enum_call_new.ts
import { Chance } from "lib-enum-call-new";
const a = Chance();
const p1: never = Chance;
const p2: never = a;
let t: Chance = null!;
const p3: never = t;
export {};
// @filename: u_enum_method.ts
import { Chance } from "lib-enum-method";
const a = Chance();
const p1: never = Chance;
const p2: never = a;
let t: Chance = null!;
const p3: never = t;
export {};
// @filename: u_enum_optional.ts
import { Chance } from "lib-enum-optional";
const a = Chance();
const p1: never = Chance;
const p2: never = a;
let t: Chance = null!;
const p3: never = t;
export {};
// @filename: u_enum_prop_other.ts
import { Chance } from "lib-enum-prop-other";
const a = Chance();
const p1: never = Chance;
const p2: never = a;
let t: Chance = null!;
const p3: never = t;
export {};
// @filename: u_function_call.ts
import { Chance } from "lib-function-call";
const a = Chance();
const p1: never = Chance;
const p2: never = a;
let t: Chance = null!;
const p3: never = t;
export {};
// @filename: u_function_call_new.ts
import { Chance } from "lib-function-call-new";
const a = Chance();
const p1: never = Chance;
const p2: never = a;
let t: Chance = null!;
const p3: never = t;
export {};
// @filename: u_function_method.ts
import { Chance } from "lib-function-method";
const a = Chance();
const p1: never = Chance;
const p2: never = a;
let t: Chance = null!;
const p3: never = t;
export {};
// @filename: u_function_optional.ts
import { Chance } from "lib-function-optional";
const a = Chance();
const p1: never = Chance;
const p2: never = a;
let t: Chance = null!;
const p3: never = t;
export {};
// @filename: u_function_prop_other.ts
import { Chance } from "lib-function-prop-other";
const a = Chance();
const p1: never = Chance;
const p2: never = a;
let t: Chance = null!;
const p3: never = t;
export {};
// @filename: u_namespace_value_call.ts
import { Chance } from "lib-namespace-value-call";
const a = Chance();
const p1: never = Chance;
const p2: never = a;
let t: Chance = null!;
const p3: never = t;
export {};
// @filename: u_namespace_value_call_new.ts
import { Chance } from "lib-namespace-value-call-new";
const a = Chance();
const p1: never = Chance;
const p2: never = a;
let t: Chance = null!;
const p3: never = t;
export {};
// @filename: u_namespace_value_method.ts
import { Chance } from "lib-namespace-value-method";
const a = Chance();
const p1: never = Chance;
const p2: never = a;
let t: Chance = null!;
const p3: never = t;
export {};
// @filename: u_namespace_value_optional.ts
import { Chance } from "lib-namespace-value-optional";
const a = Chance();
const p1: never = Chance;
const p2: never = a;
let t: Chance = null!;
const p3: never = t;
export {};
// @filename: u_namespace_value_prop_other.ts
import { Chance } from "lib-namespace-value-prop-other";
const a = Chance();
const p1: never = Chance;
const p2: never = a;
let t: Chance = null!;
const p3: never = t;
export {};
