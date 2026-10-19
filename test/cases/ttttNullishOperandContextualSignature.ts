// The operands of || and ?? are checked with the context a contextual signature sees and skipped by the first inference
// pass when context sensitive, like a conditional's branches: the function gets the signature of the branch the call's
// inference picks, `Resolver<Bd>`, not `Resolver<object>`. supabase studio's CreateCustomOidcDialog.test.tsx
// `response ?? (async ({ request }) => HttpResponse.json(...))`
// @strict: true
declare class Box<T> { t: T; static of<T extends object | null>(body?: NoInfer<T>): Box<T> }
type Resolver<R> = (info: { request: string }) => Box<R> | undefined;
type Bd = { id: string };
type Paths = { a: Bd };
declare function add<P extends keyof Paths>(a: Paths[P] extends never ? { path: P; response?: Resolver<object> } : { path: P; response: Resolver<Paths[P]> }): void;
declare const response: (() => undefined) | undefined;
add({ path: 'a', response: response ?? (({ request }) => Box.of({ id: "0" })) });
add({ path: 'a', response: (({ request }) => Box.of({ id: "0" })) });
add({ path: 'a', response: response || (({ request }) => Box.of({ id: "0" })) });
