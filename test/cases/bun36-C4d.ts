// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"], "module": "esnext", "moduleResolution": "bundler" }, "files": ["a.ts"] }
// @filename: a.ts
import { which as w1 } from "v1"; const x1: 0 = w1;
import { which as w2 } from "v2"; const x2: 0 = w2;
import { which as w3 } from "v3"; const x3: 0 = w3;
import { which as w4 } from "v4"; const x4: 0 = w4;
import { which as w5 } from "v5"; const x5: 0 = w5;
import { which as w6 } from "v6"; const x6: 0 = w6;
import { which as w7 } from "v7"; const x7: 0 = w7;
import { which as w8 } from "v8"; const x8: 0 = w8;
import { which as w9 } from "v9"; const x9: 0 = w9;
import { which as w10 } from "v10"; const x10: 0 = w10;
import { which as w11 } from "v11"; const x11: 0 = w11;
import { which as w12 } from "v12"; const x12: 0 = w12;
import { which as w13 } from "v13"; const x13: 0 = w13;
import { which as w14 } from "v14"; const x14: 0 = w14;
import { which as w15 } from "v15"; const x15: 0 = w15;
import { which as w16 } from "v16"; const x16: 0 = w16;
import { which as w17 } from "v17"; const x17: 0 = w17;
import { which as w18 } from "v18"; const x18: 0 = w18;
import { which as w19 } from "v19"; const x19: 0 = w19;
import { which as w20 } from "v20"; const x20: 0 = w20;
import { which as w21 } from "v21"; const x21: 0 = w21;
import { which as w22 } from "v22"; const x22: 0 = w22;
import { which as w23 } from "v23"; const x23: 0 = w23;
const sentinel: number = "";
// @filename: node_modules/v1/index.d.ts
export declare const which: "index [>= 4.0]";
// @filename: node_modules/v1/package.json
{"name":"v1","version":"1.0.0","typesVersions":{">= 4.0":{"*":["t.d.ts"]}}}
// @filename: node_modules/v1/t.d.ts
export declare const which: "t [>= 4.0]";
// @filename: node_modules/v10/index.d.ts
export declare const which: "index [~>7.0]";
// @filename: node_modules/v10/package.json
{"name":"v10","version":"1.0.0","typesVersions":{"~>7.0":{"*":["t.d.ts"]}}}
// @filename: node_modules/v10/t.d.ts
export declare const which: "t [~>7.0]";
// @filename: node_modules/v11/index.d.ts
export declare const which: "index [>x]";
// @filename: node_modules/v11/package.json
{"name":"v11","version":"1.0.0","typesVersions":{">x":{"*":["t.d.ts"]}}}
// @filename: node_modules/v11/t.d.ts
export declare const which: "t [>x]";
// @filename: node_modules/v12/index.d.ts
export declare const which: "index [<*]";
// @filename: node_modules/v12/package.json
{"name":"v12","version":"1.0.0","typesVersions":{"<*":{"*":["t.d.ts"]}}}
// @filename: node_modules/v12/t.d.ts
export declare const which: "t [<*]";
// @filename: node_modules/v13/index.d.ts
export declare const which: "index [> 4]";
// @filename: node_modules/v13/package.json
{"name":"v13","version":"1.0.0","typesVersions":{"> 4":{"*":["t.d.ts"]}}}
// @filename: node_modules/v13/t.d.ts
export declare const which: "t [> 4]";
// @filename: node_modules/v14/index.d.ts
export declare const which: "index [<= 8]";
// @filename: node_modules/v14/package.json
{"name":"v14","version":"1.0.0","typesVersions":{"<= 8":{"*":["t.d.ts"]}}}
// @filename: node_modules/v14/t.d.ts
export declare const which: "t [<= 8]";
// @filename: node_modules/v15/index.d.ts
export declare const which: "index [^ 7]";
// @filename: node_modules/v15/package.json
{"name":"v15","version":"1.0.0","typesVersions":{"^ 7":{"*":["t.d.ts"]}}}
// @filename: node_modules/v15/t.d.ts
export declare const which: "t [^ 7]";
// @filename: node_modules/v16/index.d.ts
export declare const which: "index [~ 7]";
// @filename: node_modules/v16/package.json
{"name":"v16","version":"1.0.0","typesVersions":{"~ 7":{"*":["t.d.ts"]}}}
// @filename: node_modules/v16/t.d.ts
export declare const which: "t [~ 7]";
// @filename: node_modules/v17/index.d.ts
export declare const which: "index [=v7]";
// @filename: node_modules/v17/package.json
{"name":"v17","version":"1.0.0","typesVersions":{"=v7":{"*":["t.d.ts"]}}}
// @filename: node_modules/v17/t.d.ts
export declare const which: "t [=v7]";
// @filename: node_modules/v18/index.d.ts
export declare const which: "index [>=v4]";
// @filename: node_modules/v18/package.json
{"name":"v18","version":"1.0.0","typesVersions":{">=v4":{"*":["t.d.ts"]}}}
// @filename: node_modules/v18/t.d.ts
export declare const which: "t [>=v4]";
// @filename: node_modules/v19/index.d.ts
export declare const which: "index [>=4 || foo]";
// @filename: node_modules/v19/package.json
{"name":"v19","version":"1.0.0","typesVersions":{">=4 || foo":{"*":["t.d.ts"]}}}
// @filename: node_modules/v19/t.d.ts
export declare const which: "t [>=4 || foo]";
// @filename: node_modules/v2/index.d.ts
export declare const which: "index [v7.0.2]";
// @filename: node_modules/v2/package.json
{"name":"v2","version":"1.0.0","typesVersions":{"v7.0.2":{"*":["t.d.ts"]}}}
// @filename: node_modules/v2/t.d.ts
export declare const which: "t [v7.0.2]";
// @filename: node_modules/v20/index.d.ts
export declare const which: "index [foo || >=4]";
// @filename: node_modules/v20/package.json
{"name":"v20","version":"1.0.0","typesVersions":{"foo || >=4":{"*":["t.d.ts"]}}}
// @filename: node_modules/v20/t.d.ts
export declare const which: "t [foo || >=4]";
// @filename: node_modules/v21/index.d.ts
export declare const which: "index [ - 8]";
// @filename: node_modules/v21/package.json
{"name":"v21","version":"1.0.0","typesVersions":{" - 8":{"*":["t.d.ts"]}}}
// @filename: node_modules/v21/t.d.ts
export declare const which: "t [ - 8]";
// @filename: node_modules/v22/index.d.ts
export declare const which: "index [4 -8]";
// @filename: node_modules/v22/package.json
{"name":"v22","version":"1.0.0","typesVersions":{"4 -8":{"*":["t.d.ts"]}}}
// @filename: node_modules/v22/t.d.ts
export declare const which: "t [4 -8]";
// @filename: node_modules/v23/index.d.ts
export declare const which: "index [4- 8]";
// @filename: node_modules/v23/package.json
{"name":"v23","version":"1.0.0","typesVersions":{"4- 8":{"*":["t.d.ts"]}}}
// @filename: node_modules/v23/t.d.ts
export declare const which: "t [4- 8]";
// @filename: node_modules/v3/index.d.ts
export declare const which: "index [latest]";
// @filename: node_modules/v3/package.json
{"name":"v3","version":"1.0.0","typesVersions":{"latest":{"*":["t.d.ts"]}}}
// @filename: node_modules/v3/t.d.ts
export declare const which: "t [latest]";
// @filename: node_modules/v4/index.d.ts
export declare const which: "index [foo]";
// @filename: node_modules/v4/package.json
{"name":"v4","version":"1.0.0","typesVersions":{"foo":{"*":["t.d.ts"]}}}
// @filename: node_modules/v4/t.d.ts
export declare const which: "t [foo]";
// @filename: node_modules/v5/index.d.ts
export declare const which: "index [07]";
// @filename: node_modules/v5/package.json
{"name":"v5","version":"1.0.0","typesVersions":{"07":{"*":["t.d.ts"]}}}
// @filename: node_modules/v5/t.d.ts
export declare const which: "t [07]";
// @filename: node_modules/v6/index.d.ts
export declare const which: "index [7.0.2.1]";
// @filename: node_modules/v6/package.json
{"name":"v6","version":"1.0.0","typesVersions":{"7.0.2.1":{"*":["t.d.ts"]}}}
// @filename: node_modules/v6/t.d.ts
export declare const which: "t [7.0.2.1]";
// @filename: node_modules/v7/index.d.ts
export declare const which: "index [>=4.0,<8]";
// @filename: node_modules/v7/package.json
{"name":"v7","version":"1.0.0","typesVersions":{">=4.0,<8":{"*":["t.d.ts"]}}}
// @filename: node_modules/v7/t.d.ts
export declare const which: "t [>=4.0,<8]";
// @filename: node_modules/v8/index.d.ts
export declare const which: "index [==7.0.2]";
// @filename: node_modules/v8/package.json
{"name":"v8","version":"1.0.0","typesVersions":{"==7.0.2":{"*":["t.d.ts"]}}}
// @filename: node_modules/v8/t.d.ts
export declare const which: "t [==7.0.2]";
// @filename: node_modules/v9/index.d.ts
export declare const which: "index [!7]";
// @filename: node_modules/v9/package.json
{"name":"v9","version":"1.0.0","typesVersions":{"!7":{"*":["t.d.ts"]}}}
// @filename: node_modules/v9/t.d.ts
export declare const which: "t [!7]";
