// An object literal whose type is a union (a spread of a union in it) against a union target elaborates into the
// member whose keys overlap the literal's most (tsc's getBestMatchingType -> findMostOverlappyType: a union source's
// keys are the ones all its members have), so the error is at the offending property (vscode's
// copilotChatSessionsProvider.ts `return { owner, repo, ...issueInfo, pullRequest: {...} }` against
// `IGitHubInfo | undefined` under exactOptionalPropertyTypes)
// @strict: true
// @exactOptionalPropertyTypes: true
interface R { readonly owner: string; readonly issues?: string[]; readonly pullRequest?: { readonly number: number; readonly icon?: string } }
declare const issues: string[];
declare const icon: string | undefined;
const issueInfo = issues.length ? { issues } : {};
export function f(): R | undefined {
  return { owner: "a", ...issueInfo, pullRequest: { number: 1, icon } };
}
declare const u: { issues: string[] } | { issues?: never };
export function k(): R | undefined {
  return { owner: "a", ...u, pullRequest: { number: "1" } };
}
