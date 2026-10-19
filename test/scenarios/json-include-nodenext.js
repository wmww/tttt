currentDirectory::/home/src/workspaces/project
useCaseSensitiveFileNames::true
Input::
//// [/home/src/workspaces/project/package.json] *new* 
{"name":"p","version":"1.0.0","exports":{"a":1}}
//// [/home/src/workspaces/project/tsconfig.json] *new* 
{ "compilerOptions": { "composite": true, "module": "nodenext", "types": [], "outDir": "out", "rootDir": "./" }, "include": ["test/**/*.ts", "package.json"] }
//// [/home/src/workspaces/project/test/a.ts] *new* 
export async function f() {
  const { default: pkg } = await import('../package.json', { with: { type: 'json' } });
  return pkg.name
}

tsgo -p tsconfig.json --noEmit
ExitStatus:: Success
Output::


