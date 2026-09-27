// @module: commonjs
// @esModuleInterop: true
// @filename: sa.d.ts
declare class SARequest {
    send(): this;
}
declare namespace request {
    interface Request extends SARequest {}
    interface SuperAgentStatic {
        Request: typeof SARequest;
    }
}
declare const request: request.SuperAgentStatic;
export = request;
// @filename: test.d.ts
import { Request } from "./sa";
declare class Test extends Request {
    end(): this;
}
export = Test;
// @filename: main.ts
import Test = require("./test");
declare const t: Test;
t.send();
t.end();
