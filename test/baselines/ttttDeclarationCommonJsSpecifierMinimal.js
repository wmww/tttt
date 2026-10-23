//// [test/cases/ttttDeclarationCommonJsSpecifierMinimal.ts] ////

//// [package.json]
{ "name": "app" }
//// [rlpxserver.ts]
export class RlpxServer { started = false; }
//// [index.ts]
export * from "./rlpxserver.ts";
//// [config.ts]
import { RlpxServer } from "./net/server/index.ts";
export class Config { server: RlpxServer | undefined = undefined; }
//// [client.ts]
import { Config } from "./config.ts";
export class Client {
    constructor(readonly config: Config) {}
    server() { return this.config.server; }
}




//// [rlpxserver.d.ts]
export declare class RlpxServer {
    started: boolean;
}
//// [index.d.ts]
export * from "./rlpxserver.ts";
//// [config.d.ts]
import { RlpxServer } from "./net/server/index.ts";
export declare class Config {
    server: RlpxServer | undefined;
}
//// [client.d.ts]
import { Config } from "./config.ts";
export declare class Client {
    readonly config: Config;
    constructor(config: Config);
    server(): import("./net/server").RlpxServer | undefined;
}
