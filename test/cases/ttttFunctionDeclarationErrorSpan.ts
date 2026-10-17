// An error at a function declaration's current node (here an overload's compatibility with the implementation, checked
// at the declaration) goes at its name, an anonymous default export's at its first token (tsc's getErrorRangeForNode)
type Rec<T, N extends 1[] = []> = N["length"] extends 150 ? T : T extends string ? Rec<T, [...N, 1]> | 0 : 1;
type Rec2<T, N extends 1[] = []> = N["length"] extends 150 ? T : T extends string ? Rec2<T, [...N, 1]> | 0 : 1;
function f<T>(x: T): Rec<T>;
function f(x: any): number { return 0; }
export default function <T>(x: T): Rec2<T>;
export default function (x: any): number { return 0; }
