# The conditional type-variable walk infers method return types (dexie's TS2615/TS2502)

`definitely_unrelated()` / `definitely_related()` in `src/ty/conditional.c` collect the type variables a conditional's
check type mentions with `mentions_type_variables()`, which resolves every signature of a function type, including a
return type still to be inferred from the body. tsc never looks there: `getPermissiveInstantiation` instantiates
lazily, and relating to `(...args: any[]) => any` never asks for the source's return type.

Repro (source sweep): `dexie/samples/dexie-cloud-todo-app` (and the `dexie` root project). `TodoList extends
Entity<TodoDB>`, `TodoDB.todoLists: DexieCloudTable<TodoList, 'id'>` = `InsertType<...>` = `Omit<T, NonInsertProps<T>>`,
whose `T[P] extends (...args: any[]) => any ? P : ...` for P = `changeOwner` (an async method without a return type)
infers its body, which reads `db.todoLists`: TS2502 on `todoLists`, TS2615 in the mapped type. tsc is clean (only the
project's 28 TS2786).

Tried: returning "unsure" from the walk when a function's return type isn't inferred yet (so the plain relation
decides). Fixed dexie, but regressed excalidraw (TS7006/TS2493/TS2345 in vitest `mockImplementationOnce` callbacks) and
vscode (1): "unsure" also makes `definitely_related` relate without the restrictive instantiation and report
`mentions`, which changes other conditionals' answers. A real fix probably makes the walk lazy like tsc's permissive
instantiation (instantiate the check type with the wildcard mapper without walking, relate, and only resolve what the
relation asks for), which is plan-sized: definitely_related's restrictive instantiation needs the same.
