// tsgo's getNamedMembers puts the inherited members whose declaration's range lies inside the class's range first
// (isDeclarationContainedBy compares offsets, not files), each range from its full start, the trivia before it
// included: c is inside M's range, b's comment starts before it (babel-parser's mixins' `comments`, dtsdiff)
// @declaration: true
// @emitDeclarationOnly: true
// @filename: base.ts
export class Base {
    declare a: number;
    // ------------------------------------------------------------ a long comment before b
    declare b: number;
    declare c: number;
}
// @filename: mixin.ts
//xxxxxxxxxxxxxxxx
import { Base } from "./base";
export const mix = (sc: typeof Base) =>
    class M extends sc {
        own = 1;
        // yyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyy
    };
