// @moduleResolution: nodenext
// @module: es2015
// @noEmit: true

// Outside node16-nodenext a file's emit format comes from package.json's `type` only when it is "module" or
// "commonjs" (tsgo's GetImpliedNodeFormatForEmitWorker); `"type": "moduleX"` decides nothing, so the import is the
// module kind's, ESM, and a bare node_modules file isn't found by ESM resolution (TS2307)
// @filename: /a/node_modules/foo.d.ts
export declare let x: number

// @filename: /a/b/package.json
{
    "name": "b",
    "type": "moduleX"
}
// @filename: /a/b/app.ts
import {x} from "foo";

// @filename: /a/c/package.json
{
    "name": "c",
    "type": "commonjs"
}
// @filename: /a/c/app.ts
import {x} from "foo";
