// @strict: true
// @lib: es2022
// @noEmit: true

// A binding element's contextual type is of the pattern's contextual type as it is (tsc's
// getContextualTypeForBindingElement, no getApparentTypeOfContextualType): an array pattern's element of a type
// parameter is what iterating it gives (`F1 | F2` of a tuple constraint: no contextual signature), an object
// pattern's property is of its apparent type
type F1 = (x: number) => void; type F2 = (y: string) => void;
function h1<T extends [F1, F2]>(t: T) { const [a = x => {}, b = y => {}] = t; }
function h2<T extends [F1, F2]>(t: T) { const [a = x => {}] = t; }
function h3<T extends F1[]>(t: T) { const [a = x => {}] = t; }
function h4<T extends {p: F1}>(t: T) { const {p = x => {}} = t; }
function h5<T extends [F1]>(t: T) { const [a = x => {}] = t; }
type C<U> = U extends 1 ? [F1, F2] : [F1, F2];
function h6<U>(t: C<U>) { const [a = x => {}] = t; }
function h7(t: [F1, F2]) { const [a = x => {}] = t; }
function h8<T extends [F1, F2]>([a = x => {}]: T) {}
