// origin: npm dmptranscripts@1.0.0 https://github.com/Deneyap-Mezunlar/dmptranscripts tsconfig.bench.json seed 20261031
// flip: no
// discord-api-types 0.33.5 payloads/v10 (and v9): APIPartialGuild extends Pick<APIGuild, 'welcome_screen'> and APIGuild
// extends APIPartialGuild. tsgo: keyof APIGuild has the inherited members, the invite's Pick<APIGuild, ...> is fine.
// tttt: TS2344, 'icon' (inherited from APIPartialGuild) not in keyof APIGuild.
// @strict: true
// @filename: guild.d.ts
export interface APIUnavailableGuild { id: string; unavailable: boolean; }
export interface APIPartialGuild extends Omit<APIUnavailableGuild, 'unavailable'>, Pick<APIGuild, 'welcome_screen'> {
    name: string;
    icon: string | null;
}
export interface APIGuild extends APIPartialGuild {
    owner: boolean;
    welcome_screen?: { description: string };
}
// @filename: invite.d.ts
import type { APIGuild } from './guild';
export declare type APIInviteGuild = Pick<APIGuild, 'id' | 'name' | 'icon' | 'owner'>;
