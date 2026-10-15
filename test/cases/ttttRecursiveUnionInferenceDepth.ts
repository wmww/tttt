// @strict: true
// A union target met again deeper in an inference walk (`RecBox<T>` inside `Box<RecBox<T>>`) still finds its object
// member in progress, a circularity, and infers the source at that level to the naked `T` too: candidates from three
// levels, the common supertype the last, `{ value: { value: typeof b4 } }` (recursiveConditionalTypes)
interface Box<T> { value: T };
type RecBox<T> = T | Box<RecBox<T>>;
declare function unbox<T>(box: RecBox<T>): T
declare let b4: { value: { value: { value: typeof b4 }}};
const r = unbox(b4);
