//// [test/cases/ttttDeclarationReusedNameUnsafeImport.ts] ////

//// [package.json]
{ "name": "client", "types": "index.d.ts" }
//// [index.d.ts]
import type { components } from "openapi-types";
export declare function get(): Promise<{ user: components["schemas"]["user"]; id: number }>;
//// [package.json]
{ "name": "openapi-types", "types": "index.d.ts" }
//// [index.d.ts]
export interface components {
    schemas: {
        user: { login: string; site_admin: boolean };
    };
}
//// [index.ts]
import { get } from "client";
export async function pullRequest() {
    return (await get());
}


//// [index.js]
"use strict";
Object.defineProperty(exports, "__esModule", { value: true });
exports.pullRequest = pullRequest;
const client_1 = require("client");
async function pullRequest() {
    return (await (0, client_1.get)());
}


//// [index.d.ts]
export declare function pullRequest(): Promise<{
    user: {
        login: string;
        site_admin: boolean;
    };
    id: number;
}>;
