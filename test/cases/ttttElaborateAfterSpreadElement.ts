// @noEmit: true
// An element after a spread of a union of tuples is named by its own type in the elaboration (tsc's
// checkExpressionForMutableLocationWithContextualType of the literal), not the re-read tuple's `E | undefined` there:
// TS2375 (vscode agentHostSessionPermissions.ts under exactOptionalPropertyTypes)
// @exactOptionalPropertyTypes: true
// @strict: true
interface P { id: string; isAllowAll?: boolean; locked?: boolean; lockedReason?: string }
declare const flag: boolean;
declare const r: boolean;
declare function reason(): string;
function f(): readonly P[] {
    return [{ id: 'a' }, ...(flag ? [{
        id: 'c', locked: r, lockedReason: r ? reason() : undefined,
    }] : []), { id: 'e', isAllowAll: true, locked: r, lockedReason: r ? reason() : undefined }];
}
