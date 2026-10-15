// A property whose name is an enum member's value is written as that member (`[Id.A]`) where the enum is
// accessible, which tracks the enum: its import is kept (vscode's chatProgressConfiguration.d.ts lost
// `ChatConfiguration` for `[ChatConfiguration.PersistentProgress]: {...}`)
// @declaration: true
// @filename: /e.ts
export const enum Id { A = "a", B = "b" }
// @filename: /c.ts
import { Id } from "./e";
export const props = {
    [Id.A]: { type: "string" },
};
