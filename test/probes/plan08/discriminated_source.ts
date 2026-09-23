// @strict: true
enum SyntaxKind { A, B, C, D }
interface Node { readonly kind: SyntaxKind; readonly pos: number }
interface Token<TKind extends SyntaxKind> extends Node { readonly kind: TKind }
type ModifierSyntaxKind = SyntaxKind.A | SyntaxKind.B | SyntaxKind.C;
interface ModifierToken<TKind extends ModifierSyntaxKind> extends Token<TKind> {}
type Modifier = ModifierToken<SyntaxKind.A> | ModifierToken<SyntaxKind.B> | ModifierToken<SyntaxKind.C>;
declare function createToken<TKind extends ModifierSyntaxKind>(token: TKind): ModifierToken<TKind>;
declare function finishNode<T extends Node>(node: T, pos: number): T;
export function parse(kind: SyntaxKind): Modifier | undefined {
    return finishNode(createToken(kind as Modifier["kind"]), 0);
}
