// @strict: true
// A bare reference to a class whose type parameter's default is the alias being lowered (Effect's SchemaAST.AST):
// whether it defaults is syntactic, the default isn't lowered then (was TS2456 and every `ast` implicitly any)
export type AST = Union | Leaf
export class Union<M extends AST = AST> {
  readonly _tag = "Union"
  constructor(readonly types: ReadonlyArray<M>) {}
}
export class Leaf { readonly _tag = "Leaf" }
declare const ast: AST
export const tags = ast._tag === "Union" ? ast.types.map((t) => t._tag) : []
