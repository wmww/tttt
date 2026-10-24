# Random npm projects: divergences found by the second box (seeds 20261100-20261199)
Same conventions as agents/bugs/npmsample.md (that file has the first seeds of this box too); a separate file so the two
boxes' appends never conflict. One section per seed, one bullet per repro, status tags as in agents/bugs/README.md.

## Seed 20261107
- **K-dependence (priority 1)**, 0093-LeCarbonator-discordjs-choice-selectmenu-builder (tsconfig.bench.json; tsgo 6
  errors): two default runs agree, `--threads 1` differs. Defaults: an extra TS2310 "Type 'APIGuild' recursively
  references itself as a base type" at node_modules/discord-api-types/payloads/v10/guild.d.ts(74); --threads 1: an
  extra TS2344 at payloads/v10/invite.d.ts(10) instead. Neither is tsgo's. The same discord-api-types base cycle as
  npm_dmptranscripts_TS2344 (agents/bugs/npmsample.md, `APIPartialGuild extends ... Pick<APIGuild, ...>` while
  APIGuild extends APIPartialGuild): which file a checker reaches first decides whether the cycle shows as TS2310 or
  as a short member set (TS2344), so that fix likely fixes both; check this project's K-dependence with it. The clone is
  kept on the second box. To reproduce on another box: discordjs-choice-selectmenu-builder@2.1.0,
  https://github.com/LeCarbonator/discordjs-choice-selectmenu-builder at 63ad5355, candidate i=93 of
  test/npmsample/20261107.candidates.jsonl (setup.jsonl has its install and bench-config changes); `scripts/projdiff.py`
  on the clone shows both runs. [open]
- Unreduced, non-flip (the clones are kept): vuejs-core packages/compiler-sfc/src/style/pluginScoped.ts(190) missing 2
  TS7006 (tsgo 5, tttt 3); renzosunico/adonis5-gigya adonis-typings/index.ts(48) 2 extra TS2308 "Module
  'gigya/dist/lib/gigya' has already exported a member named 'RequestParams'"; menglimu/ml-components 8 extra
  TS2339/TS2551 (src/components/BaseInfo/index.tsx(75) "Property 'data_' does not exist on type 'Vue'"). [open]
- The batch's first compare crashed on the varies project (fixed: npmsample.py's NameError) after deleting the
  identical clones, so 20261107.results.jsonl has only the 5 kept projects; the other 52 of 57 were identical.
