// An untyped property of a type member list is followed by what tsc's isTypeMemberStart accepts, or the list ends
// (TS1131). The lookahead runs while the member's parser is pending: a debug build's check(!has_panding_child) held
// only once it ran in that parser (vscode, from 93f638b2f)
interface I {
    a
    b;
    c?
    f2 = 1
}
type T = { x, y }
