// @strict: true
// @module: commonjs
// `type Action = Readonly<{...}>` is a twin of the ordinary Readonly instance (see ttttAliasBodyMappedTypeIdentity),
// which any instantiation turns back into the ordinary one (tsc's couldContainTypeVariables lets every mapper through
// to a mapped type's instance): Box<string>'s `act` returns `Readonly<{ type: string; }>`, not the reused annotation
// `import("./routers.js").Action`, which an uninstantiated signature keeps
// @filename: routers.ts
export type Action = Readonly<{ type: string }>;
export interface Plain { type: string }
// @filename: actions.ts
export interface Box<P> { act(p: P): import("./routers").Action; plain(p: P): import("./routers").Plain }
export declare const direct: { act(s: string): import("./routers").Action };
declare const c: boolean;
declare const b: Box<string>;
export const mk0 = () => c ? 1 : b.act;
// @filename: main.ts
import { Box, direct } from "./actions";
declare const c: boolean;
declare const b: Box<string>;
export const mk = () => c ? 1 : b.act;
export const mk2 = () => c ? 1 : b.plain;
export const mk3 = () => c ? 1 : direct.act;
