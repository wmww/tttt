# Random npm projects found on the remote box "box3" (seeds 20261200-20261299)
Same conventions as agents/bugs/npmsample.md (one bullet per repro, root-cause guess, `[flip]` before the status tag
for flips); a separate file so the three boxes never conflict.

- Seed 20261200, auguwu/utils (@augu/utils@1.5.6, the repo's package.json is named `@noelware/utils`): spurious
  TS2307 on `import ... from '@noelware/utils'` in the package's own scripts. The package imports its own name, its
  `exports` target (dist/) isn't built, and a published copy of the same package sits in node_modules. tsgo's
  loadModuleFromSelfNameReference result is continueSearching when the export target doesn't resolve, so the
  lookup goes on into node_modules; tttt's load_from_self_name gets SEARCH_STOP from load_from_exports (which
  turns an unresolved target into a stop) and resolve_non_relative returns it. Repro
  test/cases/npm__augu_utils_TS2307.ts. Fixed (det3): load_from_self_name() now turns load_from_exports()' SEARCH_STOP into SEARCH_CONTINUE, as tsc's loadModuleFromSelfNameReference returns loadModuleFromExports' continueSearching (the node_modules walk already did). [fixed det3]
- Seed 20261201, fireproof-storage/fireproof (@fireproof/core-types-protocols-cloud@0.24.19): spurious TS18048
  "'error' is possibly 'undefined'" after `const { success, data, error } = S.safeParse(x); if (!success || !data)
  throw ... error.message` (zod 4's SafeParseResult: `{success: true, data: T, error?: never} | {success: false,
  data?: never, error}`). Either test alone agrees with tsgo. With the `||`, the false-then-true path (success
  true, then `!data` with data never falsy there) should leave nothing, so error is the error member's. Guess:
  narrowing a destructured sibling by two successive discriminant references (the binding-element parent narrowing,
  getCandidateDiscriminantPropertyAccess) doesn't compose across `||` in tttt. Repro
  test/cases/npm__fireproof_core-types-protocols-cloud_TS18048.ts. (claimed: fixE) Cause: tttt asked whether `data` is a discriminant of the computed type (the success member alone after `!success`), tsc of the declared union while the computed type is a subset of it (getDiscriminantPropertyAccess); the pattern and parameter pseudo-references missed that. [fixed ada65342c]
- Seed 20261201, abrevior/apollo-federation-file-upload (@abrevior/apollo-federation-upload@4.2.0): spurious TS7016
  for `import ... from 'graphql-upload/GraphQLUpload.mjs'` (a JS-only package reached through `exports`), plus tsgo's
  20 errors in the package's own .mjs files (TS2554, TS2694) missing. allowJs + maxNodeModuleJsDepth 10 and a `paths`
  `"*": ["./*"]` (the bench config's rewrite of baseUrl "."): tsgo loads the .mjs into the program, and tttt treats it
  as an untyped JS module. Without the paths tttt agrees, so the guess is that a node_modules result reached after a
  failed paths attempt loses its external-library/JS-depth handling. Repro
  test/cases/npm__abrevior_apollo-federation-upload_TS7016.ts (the missing errors in the package's JS should follow
  from the same cause; not reproduced separately). Fixed: not paths. tttt ignored every number in a tsconfig's compilerOptions (no JSON_NUMBER case in tsconfig.c), so maxNodeModuleJsDepth stayed 0 from a config; the harness reads it from a header and was fine. [fixed corF 2026-10-24]
- Seed 20261202, ragokan/bunicorn (@bunicorn/server@0.0.30): spurious TS6196 "'T' is declared but never used" for
  `serve<T>(options: Omit<ServeOptions & Serve<T>, "fetch">)`, where `Serve` is a namespace (TS2709). tsgo's
  checkTypeReferenceNode still checks the type arguments of a reference that fails to resolve, which marks T
  referenced. tttt skips them. Repro test/cases/npm__bunicorn_server_TS6196.ts. Fixed (det3): several_types.c's TS2709 return now looks up the type arguments first. [fixed det3]
