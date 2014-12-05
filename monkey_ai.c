#include "monkey_ai.h"

#include "asss.h"
#include "fake.h"
#include "packets/kill.h"

#include <string.h>
#include <stdio.h>
#include <math.h>

local Imodman *mm;
local Ifake *fake;
local Ilogman *lm;
local Iarenaman *aman;
local Ichat *chat;
local Icmdman *cmd;
local Imainloop *ml;
local Igame *game;
local Iplayerdata *pd;
local Iconfig *config;
local Imapdata *map;
local Iprng *prng;
local Inet *net;

#define MODULE_NAME "monkey_ai"

local const char *ShipNames[] = { "Warbird", "Javelin", "Spider", "Leviathan",
                                  "Terrier", "Weasel", "Lancaster", "Shark" };

                                  
                                  



/** The arena configuration options that are needed for the ai. */
typedef struct {
    /** The radius for each ship. */
    int radius[8];

    /** The bullet speed for each ship. */
    int bullet_speed[8];
    
    /** Angle spread between multi-fire bullets and standard forward firing bullets 
     * (111 = 1 degree, 1000 = 1-ship-rotation-point
     */
    int multifire_angle[8];

    /* Whether ships fire with double barrel bullets. */
    int double_barrel[8];
    /** The bomb speed for each ship. */
    int bomb_speed[8];

    /** Initial energy for each ship. */
    int initial_energy[8];

    /** Upgrade energy for each ship. */
    int upgrade_energy[8];

    /** Maximum energy for each ship. */
    int max_energy[8];
    
    /** Initial recharge for each ship. */
    int initial_recharge[8];

    /** Upgrade recharge for each ship. */
    int upgrade_recharge[8];

    /** Maximum recharge for each ship. */
    int max_recharge[8];
    
    /** The amount of times the a bomb can bounce. */
    int bounce_count[8];
    
    /** The initial thrust for each ship. */
    int initial_thrust[8];

    /** The amount of bullets each bursts releases for each ship. */
    int burst_shrapnel[8];
    
    /** The speed of the burst bullets for each ship. */
    int burst_speed[8];
    
    /** The maximum amount of damage each burst bullet can do. */
    int burst_damage_level;
    
    /** The amount of time in ticks that bullets are alive. */
    int bullet_alive_time;
    
    /** The amount of time in ticks that bombs are alive. */
    int bomb_alive_time;
    
    /** Amount of damage a bomb causes at its center point. */
    int bomb_damage_level;
    
    /** Blast radius in pixels for an L1 bomb. L2 doubles, L3 tripesl. */
    int bomb_explode_pixels;
    
    /** Radius of prox trigger in tiles. */
    int proximity_distance;
    
    /** 1 if bullet damage is exact, 0 is it's random. */
    int bullet_exact_damage;
    
    /** The amount of damage a level 1 bullet will do. */
    int bullet_damage_level;
    
    /** Amount of extra damage each bullet level will cause. */
    int bullet_damage_upgrade;
    
    /** The speed at which players are repelled. */
    int repel_speed;
    
    /** Number of pixels from the player that are affected by a repel. */
    int repel_distance;
    
    /** How many ticks this repel is alive. */
    int repel_time;
    
    /** How long each death lasts in ticks. */
    int enter_delay;
} ArenaConfig;

/** The data that's associated with each arena. */
typedef struct {
    /** The list of ai players in this arena. */
    LinkedList players;

    /** The list of active weapons in this arena. */
    LinkedList weapons;
    
    /** The list of active weapons to be destroyed next tick. */
    LinkedList weapons_destroy;

    /** The configuration settings for this arena. */
    ArenaConfig config;

    /** Last time the ai was updated. */
    int last_update;
    
    /** The mutex to lock when accessing any arena data. */
    pthread_mutex_t mutex;
    
    /** The mutex attributes. Recursive mutex. */
    pthread_mutexattr_t pthread_attr;
} AIArenaData;
local int adkey;


local void ReadConfig(Arena* arena);
local void DestroyAIPlayer(LinkedList *players, AIPlayer *aip);
local void GetSpawnPoint(Arena *arena, int freq, int *spawnx, int *spawny);

/************************/

/* Defined in interface */
local void Lock(Arena *arena) {
    AIArenaData *ad = P_ARENA_DATA(arena, adkey);
    pthread_mutex_lock(&ad->mutex);    
}

/* Defined in interface */
local void Unlock(Arena *arena) {
    AIArenaData *ad = P_ARENA_DATA(arena, adkey);
    pthread_mutex_unlock(&ad->mutex);
}

/* Defined in interface */
local AIPlayer *CreateAI(Arena *arena, const char *name, int freq, int ship) {
    AIArenaData *ad = P_ARENA_DATA(arena, adkey);
    AIPlayer* aip = amalloc(sizeof(AIPlayer));

    if (*name) 
        strncpy(aip->name, name, 24);
    else 
        strncpy(aip->name, "ai", 24);
    
    int x, y;
    GetSpawnPoint(arena, freq, &x, &y);
    
    aip->ship = ship;
    aip->x = x * 16;
    aip->y = y * 16;
    aip->xspeed = 0;
    aip->yspeed = 0;
    aip->freq = freq;
    aip->rotation = 0;
    aip->last_weapon = NULL;
    aip->last_hitter = NULL;
    aip->dead = 0;
    aip->time_died = 0;
    
    pthread_mutex_lock(&ad->mutex);
    aip->energy = ad->config.initial_energy[aip->ship];
    aip->recharge = ad->config.initial_recharge[aip->ship];
    aip->target = NULL;
    
    Player *fp = fake->CreateFakePlayer(aip->name, arena, aip->ship, freq);

    if (!fp) {
        lm->LogA(L_ERROR, MODULE_NAME, arena, "Failed to create fake player for AI player.");
        afree(aip);
        aip = NULL;
    } else {
        aip->player = fp;
        LLAdd(&ad->players, aip);

        lm->LogA(L_INFO, MODULE_NAME, arena, "Created new AI player.");
        
        DO_CBS(CB_AIDAMAGE, arena, NewAIFunc, (aip));
    }
    
    pthread_mutex_unlock(&ad->mutex);
    
    return aip;
}

/* Defined in interface */
local void DestroyAI(AIPlayer *aip) {
    Arena *arena = aip->player->arena;
    
    Lock(arena);
    
    AIArenaData *ad = P_ARENA_DATA(arena, adkey);
    DestroyAIPlayer(&ad->players, aip);
    
    Unlock(arena);
}

/************************/

/** Gets a spawn location for an ai player.
 * @param arena The arena where the player is.
 * @param freq The freq that the ai player is on.
 * @param spawnx The x output location.
 * @param spawny The y output location.
 */
local void GetSpawnPoint(Arena *arena, int freq, int *spawnx, int *spawny) {
    int spawnX = config->GetInt(arena->cfg, "Spawn", "Team0-X", 512); 
    int spawnY = config->GetInt(arena->cfg, "Spawn", "Team0-Y", 512); 
    int spawn_radius = config->GetInt(arena->cfg, "Spawn", "Team0-Radius", 16); 

    if (freq >= 1 && freq <= 3) {
        char strx[32], stry[32], strrad[32];
        sprintf(strx, "Team%d-X", freq);
        sprintf(stry, "Team%d-Y", freq);
        sprintf(strrad, "Team%d-Radius", freq);

        spawnX = config->GetInt(arena->cfg, "Spawn", strx, spawnX); 
        spawnY = config->GetInt(arena->cfg, "Spawn", stry, spawnY); 
        spawn_radius = config->GetInt(arena->cfg, "Spawn", strrad, spawn_radius); 
    }

    int spawn_dist = prng->Number(0, spawn_radius);
    double spawn_rot = prng->Number(0, 359) * (M_PI / 180);

    *spawnx = spawnX + spawn_dist * cos(spawn_rot);
    *spawny = spawnY + spawn_dist * sin(spawn_rot);
}

/** Removes weapon from arena list and frees the memory.
 * @param weapons The list of weapons that are active in the arena.
 * @param weapon The weapon that should be destroyed.
 */
local void DestroyWeapon(LinkedList *weapons, EnemyWeapon* weapon) {
    LLRemove(weapons, weapon);
    afree(weapon);
}

/** Flags a weapon and its parent / children to be destroyed on next tick.
 * @param arena The arena where the weapon exists.
 * @param weapon The weapon to destroy.
 */
local void FlagWeaponForDestroy(Arena *arena, EnemyWeapon *weapon) {
    AIArenaData *ad = P_ARENA_DATA(arena, adkey);
    
    pthread_mutex_lock(&ad->mutex);
    
    EnemyWeapon *parent = weapon->parent;
    EnemyWeapon *w;
    Link *link;
    
    FOR_EACH(&ad->weapons, w, link) {
        if ((w == parent || w->parent == weapon || (parent && w->parent == parent)) && w->destroy == 0) {
            w->destroy = 1;
            LLAdd(&ad->weapons_destroy, w);
        }
    }
    
    if (weapon->destroy == 0) {
        weapon->destroy = 1;
        LLAdd(&ad->weapons_destroy, weapon);
    }
    
    pthread_mutex_unlock(&ad->mutex);
}

/** Removes ai player from arena list and frees the memory.
 * @param players The list of ai players in the arena.
 * @param aip The AI player that should be destroyed.
 */
local void DestroyAIPlayer(LinkedList *players, AIPlayer *aip) {
    fake->EndFaked(aip->player);
    LLRemove(players, aip);
    afree(aip);
}

/** Just targets the closest human in a ship for now.
 * @param aip The AIP player that is searching for a target.
 * @return The asss player that is being targetted.
 */
local Player *GetTargetPlayer(AIPlayer *aip) {
    Arena *arena = aip->player->arena;
    Player *target = NULL;
    Player *player;
    Link *link;
    
    double closest_dist = 100000;
    
    aman->Lock();
    
    FOR_EACH_PLAYER_IN_ARENA(player, arena) {
        if (!IS_HUMAN(player)) continue;
        if (player->p_freq == aip->freq) continue;
        
        if (player->p_ship != SHIP_SPEC) {
            int dx = player->position.x - aip->x;
            int dy = player->position.y - aip->y;
            double dist = sqrt(dx * dx + dy * dy);
            if (dist < closest_dist) {
                target = player;
                closest_dist = dist;
            }
        }
    }
    aman->Unlock();
    
    return target;
}

/** Does damage to all of the ai players near a bomb.
 * Arena mutex should always be locked before calling this.
 * @param arena The arena where the collision happened.
 * @param weapon The weapon that collided.
 * @param aip Optional ai player that will be ignored.
 */
local void DoSplashDamage(Arena *arena, EnemyWeapon *weapon, AIPlayer *aip) {
    AIArenaData *ad = P_ARENA_DATA(arena, adkey);
    
    int radius = ad->config.bomb_explode_pixels + ad->config.bomb_explode_pixels * weapon->level;
    
    AIPlayer *p;
    Link *link;
    
    FOR_EACH(&ad->players, p, link) {
        if (p == aip) continue;
        
        int dx = p->x - weapon->x;
        int dy = p->y - weapon->y;
        
        double dist = sqrt(dx * dx + dy * dy);
        
        if (dist < radius) {
            double damage = (radius - dist) / radius * ad->config.bomb_damage_level;
            p->energy -= damage;
            
            p->last_weapon = weapon;
        }
    }
}

/** Checks for weapon/player collisions.
 * @param aip The AI player that is being checked for weapon collisions.
 */
local void CheckWeaponHit(AIPlayer *aip) {
    AIArenaData *ad = P_ARENA_DATA(aip->player->arena, adkey);
    Link *link;
    EnemyWeapon *weapon;

    pthread_mutex_lock(&ad->mutex);
    
    FOR_EACH(&ad->weapons, weapon, link) {
        if (weapon->active == 0 || weapon->destroy == 1) continue;
        if (weapon->shooter->p_freq == aip->freq) continue;
        if (weapon->type == W_REPEL) continue;
        
        // check if hit
        int ship_radius = ad->config.radius[aip->ship];
        double wx = weapon->x;
        double wy = weapon->y;

        double x = aip->x;
        double y = aip->y;

        double dx = wx - x;
        double dy = wy - y;

        double dist = sqrt(dx * dx + dy * dy);

        int hit_dist = ship_radius * 2;
        
        if (weapon->type == W_PROXBOMB) {
            int prox_dist = ad->config.proximity_distance + weapon->level;
            hit_dist = ship_radius + prox_dist * 16;
        }
        
        if (dist <= hit_dist) {
            int damage = weapon->max_damage;
            
            if (weapon->type == W_BOMB || weapon->type == W_PROXBOMB) {
                damage = fmin(1.0, ((hit_dist - dist) / hit_dist + 0.3)) * damage;
                DoSplashDamage(aip->player->arena, weapon, aip);
            }
            
            aip->energy -= damage;
            aip->last_hitter = weapon->shooter;

            FlagWeaponForDestroy(aip->player->arena, weapon);
            
            aip->last_weapon = weapon;
            DO_CBS(CB_AIDAMAGE, aip->player->arena, AIDamageFunc, (aip, weapon));
        }
    }
    
    pthread_mutex_unlock(&ad->mutex);
}

/** Determines if the tile x, y is solid.
 * @param arena The current arena.
 * @param x The x tile to check.
 * @param y The y tile to check.
 * @return Returns 1 if it is solid, 0 otherwise.
 */
int IsSolid(Arena* arena, int x, int y) {
    enum map_tile_t type = map->GetTile(arena, x, y);

    return !(type == TILE_NONE || 
             type == TILE_SAFE ||
             type == TILE_TURF_FLAG ||
             type == TILE_GOAL ||
            (type >= TILE_OVER_START && type <= TILE_UNDER_END));
}

/** Determines if the tile x, y is in a safe zone.
 * @param arena The current arena.
 * @param x The x tile to check.
 * @param y The y tile to check.
 * @return Returns 1 if it is a safe tile, 0 otherwise.
 */
int InSafe(Arena *arena, int x, int y) {
    return map->GetTile(arena, x, y) == TILE_SAFE;
}

/** Traces along the weapon's path
 * @param weapon The weapon is that is being traced.
 * @param dt The timestep.
 * @return Returns 1 if wall collision happened, 0 otherwise.
 */
int TraceWeapon(EnemyWeapon *weapon, int dt) {
    Arena *arena = weapon->arena;
    AIArenaData *ad = P_ARENA_DATA(arena, adkey);

    // Stop moving if it hits a wall and not bouncing
    double x = weapon->x;
    double y = weapon->y;
    int solid = 0;

    double dist = (weapon->speed / 10.0) * (dt / 100.0);
    double xtravel = weapon->xspeed / 10 * (dt / 100.0);
    double ytravel = weapon->yspeed / 10 * (dt / 100.0);
    
    int tile_x = 0;
    int tile_y = 0;
    int last_tile_x = floor(x / 16.0);
    int last_tile_y = floor(y / 16.0);
    
    int ticks = current_ticks();
    
    pthread_mutex_lock(&ad->mutex);
    
    if (weapon->type == W_BULLET || weapon->type == W_BOUNCEBULLET || weapon->type == W_BURST) {
        if (ticks - weapon->created >= ad->config.bullet_alive_time) {
            // weapon time out
            FlagWeaponForDestroy(arena, weapon);
            pthread_mutex_unlock(&ad->mutex);
            return 0;
        }
    } else if (weapon->type == W_BOMB || weapon->type == W_PROXBOMB) {
        if (ticks - weapon->created >= ad->config.bomb_alive_time) {
            // weapon time out
            FlagWeaponForDestroy(arena, weapon);
            pthread_mutex_unlock(&ad->mutex);
            return 0;
        }
    }
 
    for (int i = 0; i < dist; ++i) {
        x += cos(weapon->rotation) + xtravel / dist;
        y -= sin(weapon->rotation) - ytravel / dist;
        
        tile_x = floor(x / 16.0);
        tile_y = floor(y / 16.0);
        
        if (tile_x == last_tile_x && tile_y == last_tile_y)
            continue;
            
        solid = IsSolid(arena, tile_x, tile_y);

        if (solid) {
            if (weapon->type == W_BOMB || weapon->type == W_PROXBOMB) {
                if (weapon->bouncing && weapon->bounces_left-- <= 0)
                    break;
            }
            
            if (weapon->bouncing) {
                // flip the rotation of the bullet when it collides with a tile
                
                if (weapon->type == W_BURST && weapon->active == 0)
                    weapon->active = 1;
                
                int dx = tile_x - last_tile_x;
                int dy = tile_y - last_tile_y;
                
                int below = (int)(floor(y)) % 16 < 3;
                int above = (int)(floor(y)) % 16 > 13;
                int right = (int)(floor(x)) % 16 < 3;
                int left = (int)(floor(x)) % 16 > 13;
                
                int horizontal = (below && dy > 0) || (above && dy < 0);
                int vertical = (right && dx > 0) || (left && dx < 0);
                
                double c = cos(weapon->rotation);
                double s = sin(weapon->rotation);
                
                if (horizontal)
                    s = -s;
                    
                if (vertical)
                    c = -c;
                
                weapon->rotation = atan2(s, c);
                
                solid = 0;
            } else {
                break;
            }
        }

        last_tile_x = tile_x;
        last_tile_y = tile_y;
    }

    weapon->x = x;
    weapon->y = y;
    
    pthread_mutex_unlock(&ad->mutex);
    
    return solid;
}

/** Pushes any ai players inside the repel radius.
 * @param weapon The repel weapon that is being updated.
 * @param dt The timestep.
 * @return Returns 1 if the repel has timed out, 0 otherwise.
 */
int UpdateRepel(EnemyWeapon *weapon, int dt) {
    AIArenaData *ad = P_ARENA_DATA(weapon->arena, adkey);

    int ticks = current_ticks();
    
    pthread_mutex_lock(&ad->mutex);
    if (ticks - weapon->created >= 25 + ad->config.repel_time) {
        pthread_mutex_unlock(&ad->mutex);
        return 1;   // It will be marked as destroyed in the calling function
    }
        
    AIPlayer *aip;
    Link *link;
        
    FOR_EACH(&ad->players, aip, link) {
        if (aip->freq == weapon->shooter->p_freq) continue;
        
        int dx = aip->x - weapon->x;
        int dy = aip->y - weapon->y;
        double dist = sqrt(dx * dx + dy * dy);
        
        if (dist <= ad->config.repel_distance) {
            double rot = atan2(dy, dx);
            int speed = ad->config.repel_speed;
            aip->xspeed = speed * cos(rot);
            aip->yspeed = speed * sin(rot);
        }
    }
    
    EnemyWeapon *w;
    FOR_EACH(&ad->weapons, w, link) {
        if (weapon->shooter->p_freq == weapon->shooter->p_freq) continue;
        
        int dx = w->x - weapon->x;
        int dy = w->y - weapon->y;
        double dist = sqrt(dx * dx + dy * dy);
        
        if (dist <= ad->config.repel_distance) {
            double rot = atan2(dy, dx);
            int speed = ad->config.repel_speed;
            w->xspeed = speed * cos(rot);
            w->yspeed = speed * sin(rot);
        }
    }
    
    pthread_mutex_unlock(&ad->mutex);
    return 0;
}

/** Update weapons and players by a single tick.
 * @param arena The arena to update.
 */
local void DoTick(Arena *arena) {
    AIArenaData *ad = P_ARENA_DATA(arena, adkey);
    
    Link *link;
    
    EnemyWeapon *weapon;
    
    pthread_mutex_lock(&ad->mutex);
    
    // Update each weapon 1 tick
    FOR_EACH(&ad->weapons, weapon, link) {
        // update weapon position
        if (InSafe(arena, weapon->shooter->position.x / 16, weapon->shooter->position.y / 16)) {
            // weapon owner in safe
            FlagWeaponForDestroy(arena, weapon);
            continue;
        }

        if (weapon->update(weapon, 1)) {
            if (weapon->type == W_BOMB || weapon->type == W_PROXBOMB)
                DoSplashDamage(arena, weapon, NULL);
            
            FlagWeaponForDestroy(arena, weapon);
            continue;
        }
    }
    
    // Remove weapons that are flagged to be destroyed
    FOR_EACH(&ad->weapons_destroy, weapon, link)
        DestroyWeapon(&ad->weapons, weapon);
    LLEmpty(&ad->weapons_destroy);
    
    AIPlayer *aip;
    // Update each player 1 tick
    FOR_EACH(&ad->players, aip, link) {
        const int thrust = ad->config.initial_thrust[aip->ship] * 100;
        const int Speed = 250; // pixels per second
        
        if (aip->dead) {
            if (current_ticks() - aip->time_died >= ad->config.enter_delay) {
                int x, y;
                GetSpawnPoint(arena, aip->freq, &x, &y);
                aip->dead = 0;
                aip->x = x * 16;
                aip->y = y * 16;
                aip->xspeed = 0;
                aip->yspeed = 0;
                aip->energy = ad->config.initial_energy[aip->ship];
            } else {
                continue;
            }
        }
        
        if (aip->target) {
            int dx = aip->target->position.x - aip->x;
            int dy = aip->target->position.y - aip->y;

            double angle = atan2(dy, dx);
            
            aip->rotation = angle;

            // Apply thrust
            aip->xspeed += thrust * cos(angle) * (1.0 / 100.0);
            aip->yspeed += thrust * sin(angle) * (1.0 / 100.0);

            // Cap xspeed to max speed
            aip->xspeed = fmax(fmin(aip->xspeed, Speed * 10), -Speed * 10);
            aip->yspeed = fmax(fmin(aip->yspeed, Speed * 10), -Speed * 10);
            
            double xinc = (aip->xspeed / 10) * (1.0 / 100.0);
            double yinc = (aip->yspeed / 10) * (1.0 / 100.0);
            
            if (IsSolid(arena, (aip->x + xinc) / 16, (aip->y + yinc) / 16)) {
                aip->xspeed *= -1;
                aip->yspeed *= -1;
                xinc = -xinc;
                yinc = -yinc;
            }

            // Increase bot's actual position
            aip->x += xinc;
            aip->y += yinc;
        } else {
            aip->xspeed = 0;
            aip->yspeed = 0;
        }
        
        aip->energy += aip->recharge / 10.0 * (1.0 / 100.0);
        aip->energy = fmin(aip->energy, ad->config.max_energy[aip->ship]);
        
        if (!InSafe(arena, aip->x / 16, aip->y / 16))
            CheckWeaponHit(aip);
    }
    
    pthread_mutex_unlock(&ad->mutex);
}

/*****************************/

/** Timer to update the bots.
 * @param param The arena to update.
 */
local int UpdateTimer(void *param) {
    Arena *arena = param;
    struct C2SPosition ppk = {0};
    
    AIArenaData *ad = P_ARENA_DATA(arena, adkey);

    int ticks = current_ticks();
    
    pthread_mutex_lock(&ad->mutex);
    
    int dt = ticks - ad->last_update;

    // Update players and weapons by 1 tick at a time
    for (int i = 0; i < dt; ++i)
        DoTick(arena);
        
    ad->last_update = current_ticks();
    
    ppk.type = C2S_POSITION;
    ppk.rotation = 0;
    ppk.weapon.type = W_NULL;
    ppk.x = 512 * 16;
    ppk.y = 512 * 16;
    ppk.time = current_ticks();
    ppk.xspeed = 0;
    ppk.yspeed = 0;
    ppk.energy = 500;
    ppk.status = 0;

    AIPlayer *aip;
    Link *link;

    // Send out the position packets for the ai players
    FOR_EACH(&ad->players, aip, link) {
        if (aip->energy <= 0) {
            if (!aip->dead) {
                aip->dead = 1;
                aip->time_died = current_ticks();
                
                struct KillPacket kill;
                kill.type = S2C_KILL;
                kill.green = 0;
                kill.killer = aip->last_hitter->pid;
                kill.killed = aip->player->pid;
                kill.bounty = 10;
                kill.flags = 0;
                
                net->SendToArena(arena, NULL, (u8*)&kill, sizeof(kill), NET_RELIABLE);
                
                DO_CBS(CB_AIKILL, arena, AIKillFunc, (aip->last_hitter, aip, aip->last_weapon));
            }
            continue;
        }
        
        aip->target = GetTargetPlayer(aip);

        double angle = aip->rotation * 180 / M_PI;
        int rot = (angle / 9) + 10;
        if (rot < 0) rot += 40;
        
        ppk.rotation = rot;
        ppk.x = aip->x;
        ppk.y = aip->y;
        ppk.energy = aip->energy;
        ppk.xspeed = aip->xspeed;
        ppk.yspeed = aip->yspeed;

        game->FakePosition(aip->player, &ppk, sizeof(ppk));
    }

    pthread_mutex_unlock(&ad->mutex);
    return 1;
}

/*****************************/

/** Position packet callback. Create a weapon if one was fired. */
local void OnPPK(Player *p, const struct C2SPosition *pos) {
    if (!(pos->weapon.type == W_BULLET || pos->weapon.type == W_BOUNCEBULLET ||
          pos->weapon.type == W_BOMB || pos->weapon.type == W_PROXBOMB ||
          pos->weapon.type == W_BURST || pos->weapon.type == W_REPEL)) return;
          
    Arena *arena = p->arena;
    AIArenaData *ad = P_ARENA_DATA(arena, adkey);
    
    pthread_mutex_lock(&ad->mutex);
    
    if (pos->weapon.type == W_REPEL) {
        EnemyWeapon *weapon = amalloc(sizeof(EnemyWeapon));
        weapon->x = pos->x;
        weapon->y = pos->y;
        
        weapon->type = W_REPEL;
        weapon->destroy = 0;
        weapon->created = current_ticks();
        weapon->arena = arena;
        weapon->parent = NULL;
        weapon->update = &UpdateRepel;
        weapon->shooter = p;
        weapon->active = 1;
        weapon->level = 0;
        
        LLAdd(&ad->weapons, weapon);
        
        pthread_mutex_unlock(&ad->mutex);
        return;
    } else if (pos->weapon.type == W_BURST) {
        int amount = ad->config.burst_shrapnel[p->p_ship];
        
        double rotation = 0.0;
        double rot_inc = (2.0 * M_PI) / amount;
        for (int i = 0; i < amount; ++i) {
            EnemyWeapon *weapon = amalloc(sizeof(EnemyWeapon));
            weapon->x = pos->x;
            weapon->y = pos->y;
            
            weapon->type = W_BURST;
            weapon->destroy = 0;
            weapon->created = current_ticks();
            weapon->arena = arena;
            weapon->parent = NULL;
            weapon->update = &TraceWeapon;
            weapon->shooter = p;
            weapon->active = 0;
            weapon->max_damage = ad->config.burst_damage_level;
            weapon->rotation = rotation;
            weapon->bouncing = 1;
            weapon->xspeed = 0;
            weapon->yspeed = 0;
            weapon->level = 0;
            weapon->speed = ad->config.burst_speed[p->p_ship];
            
            LLAdd(&ad->weapons, weapon);
            
            rotation += rot_inc;
        }
        
        pthread_mutex_unlock(&ad->mutex);
        return;
    }
          
    EnemyWeapon *weapon = amalloc(sizeof(EnemyWeapon));
    
    weapon->rotation = ((40 - (pos->rotation + 30) % 40) * 9) * (M_PI / 180);
    
    int radius = ad->config.radius[p->p_ship];
    
    weapon->x = pos->x + radius * cos(weapon->rotation);
    weapon->y = pos->y - radius * sin(weapon->rotation);
    weapon->xspeed = p->position.xspeed;
    weapon->yspeed = p->position.yspeed;
    weapon->type = pos->weapon.type;
    weapon->destroy = 0;
    weapon->level = pos->weapon.level;
    
    weapon->speed = ad->config.bullet_speed[p->p_ship];
    
    if (weapon->type == W_BOMB || weapon->type == W_PROXBOMB) {
        weapon->bounces_left = ad->config.bounce_count[p->p_ship];
        weapon->max_damage = ad->config.bomb_damage_level;
        
        weapon->speed = ad->config.bomb_speed[p->p_ship];
    }
    
    if (weapon->type == W_BULLET || weapon->type == W_BOUNCEBULLET) {
        int level = pos->weapon.level;
        weapon->max_damage = ad->config.bullet_damage_level + ad->config.bullet_damage_upgrade * level;
        
        if (ad->config.bullet_exact_damage == 0)
            weapon->max_damage = prng->Number(1, weapon->max_damage);
    }
    
    weapon->created = current_ticks();
    weapon->arena = arena;
    weapon->bouncing = pos->weapon.type == W_BOUNCEBULLET;
    weapon->parent = NULL;
    weapon->update = &TraceWeapon;
    weapon->shooter = p;
    weapon->active = 1;
    
    LLAdd(&ad->weapons, weapon);
    
    if (weapon->type == W_BULLET || weapon->type == W_BOUNCEBULLET) {
        if (ad->config.double_barrel[p->p_ship]) {
            int offset = radius * 0.7;
            
            int xoffset = offset * sin(weapon->rotation);
            int yoffset = offset * cos(weapon->rotation);
            
            EnemyWeapon *other = amalloc(sizeof(EnemyWeapon));
            *other = *weapon;
            
            weapon->x += xoffset;
            weapon->y += yoffset;
            
            other->x -= xoffset;
            other->y -= yoffset;
            
            other->parent = weapon;
            
            LLAdd(&ad->weapons, other);
        }
        
        if (pos->weapon.alternate == 1) {
            // multifire
            double angle = (ad->config.multifire_angle[p->p_ship] / 111.0) * (M_PI / 180);
            
            EnemyWeapon *first = amalloc(sizeof(EnemyWeapon));
            *first = *weapon;
            
            EnemyWeapon *second = amalloc(sizeof(EnemyWeapon));
            *second = *weapon;
            
            first->rotation = weapon->rotation + angle;
            second->rotation = weapon->rotation - angle;
            
            first->x = pos->x + radius * cos(first->rotation);
            first->y = pos->y - radius * sin(first->rotation);
            
            second->x = pos->x + radius * cos(second->rotation);
            second->y = pos->y - radius * sin(second->rotation);
            
            first->parent = weapon;
            second->parent = weapon;
            
            LLAdd(&ad->weapons, first);
            LLAdd(&ad->weapons, second);
        }
    }
    
    pthread_mutex_unlock(&ad->mutex);
}

/** Arena action callback. Reload the configuration settings. */
local void OnArenaAction(Arena *arena, int action) {
    if (action == AA_CREATE || action == AA_CONFCHANGED)
        ReadConfig(arena);
}

/*****************************/

/** Reloads the configuration settings.
 * @param arena The arena whose config settings should be reloaded.
 */
local void ReadConfig(Arena* arena) {
    AIArenaData *ad = P_ARENA_DATA(arena, adkey);
    
    pthread_mutex_lock(&ad->mutex);
    
    for (int i = 0; i < 8; ++i) {
        ad->config.radius[i] = config->GetInt(arena->cfg, ShipNames[i], "Radius", 14);
        ad->config.bullet_speed[i] = config->GetInt(arena->cfg, ShipNames[i], "BulletSpeed", 2000);
        ad->config.bomb_speed[i] = config->GetInt(arena->cfg, ShipNames[i], "BombSpeed", 2000);
        
        ad->config.initial_energy[i] = config->GetInt(arena->cfg, ShipNames[i], "InitialEnergy", 1000);
        ad->config.upgrade_energy[i] = config->GetInt(arena->cfg, ShipNames[i], "UpgradeEnergy", 100);
        ad->config.max_energy[i] = config->GetInt(arena->cfg, ShipNames[i], "MaximumEnergy", 1700);
        
        ad->config.initial_recharge[i] = config->GetInt(arena->cfg, ShipNames[i], "InitialRecharge", 400);
        ad->config.upgrade_recharge[i] = config->GetInt(arena->cfg, ShipNames[i], "UpgradeRecharge", 166);
        ad->config.max_recharge[i] = config->GetInt(arena->cfg, ShipNames[i], "MaximumRecharge", 1150);
        
        ad->config.bounce_count[i] = config->GetInt(arena->cfg, ShipNames[i], "BombBounceCount", 0);
        ad->config.initial_thrust[i] = config->GetInt(arena->cfg, ShipNames[i], "InitialThrust", 15);
        ad->config.double_barrel[i] = config->GetInt(arena->cfg, ShipNames[i], "DoubleBarrel", 0);
        ad->config.multifire_angle[i] = config->GetInt(arena->cfg, ShipNames[i], "MultiFireAngle", 500);
        
        ad->config.burst_shrapnel[i] = config->GetInt(arena->cfg, ShipNames[i], "BurstShrapnel", 24);
        ad->config.burst_speed[i] = config->GetInt(arena->cfg, ShipNames[i], "BurstSpeed", 3000);
    }

    ad->config.bullet_alive_time = config->GetInt(arena->cfg, "Bullet", "BulletAliveTime", 550);
    ad->config.bullet_damage_level = config->GetInt(arena->cfg, "Bullet", "BulletDamageLevel", 200);
    ad->config.bullet_damage_upgrade = config->GetInt(arena->cfg, "Bullet", "BulletDamageUpgrade", 100);
    ad->config.bullet_exact_damage = config->GetInt(arena->cfg, "Bullet", "ExactDamage", 0);
    
    ad->config.bomb_alive_time = config->GetInt(arena->cfg, "Bomb", "BombAliveTime", 8000);
    ad->config.bomb_damage_level = config->GetInt(arena->cfg, "Bomb", "BombDamageLevel", 7500);
    ad->config.bomb_explode_pixels = config->GetInt(arena->cfg, "Bomb", "BombExplodePixels", 80);
    ad->config.proximity_distance = config->GetInt(arena->cfg, "Bomb", "ProximityDistance", 3);
    
    ad->config.repel_distance = config->GetInt(arena->cfg, "Repel", "RepelDistance", 512);
    ad->config.repel_speed = config->GetInt(arena->cfg, "Repel", "RepelSpeed", 5000);
    ad->config.repel_time = config->GetInt(arena->cfg, "Repel", "RepelTime", 225);
    
    ad->config.enter_delay = config->GetInt(arena->cfg, "Kill", "EnterDelay", 200);
    
    ad->config.burst_damage_level = config->GetInt(arena->cfg, "Burst", "BurstDamageLevel", 700);
    
    pthread_mutex_unlock(&ad->mutex);
}

/*****************************/

local helptext_t help_createai =
"Module: monkey_ai\n"
"Targets: \n"
"Args: [name]\n"
"Creates a new ai player.\n";
local void Ccreateai(const char *command, const char *params, Player *p, const Target *target) {
    AIPlayer* aip = CreateAI(p->arena, params, 100, 1);
    
    if (!aip)
        chat->SendMessage(p, "Failed to create fake player for ai player.");
    else
        chat->SendMessage(p, "Created new ai player.");
}

local helptext_t help_removeai =
"Module: monkey_ai\n"
"Targets: ai bot"
"Args: \n"
"Removes target ai bot from the arena.\n";
local void Cremoveai(const char *command, const char *params, Player *p, const Target *target) {
    if (!target || target->type != T_PLAYER) {
        chat->SendMessage(p, "Target is not an ai bot.");
        return;
    }

    AIArenaData *ad = P_ARENA_DATA(p->arena, adkey);
    
    Player *target_player = target->u.p;

    Link *link;
    AIPlayer *aip;
    
    pthread_mutex_lock(&ad->mutex);
    
    int removed = 0;
    
    FOR_EACH(&ad->players, aip, link) {
        if (aip->player == target_player) {
            DestroyAIPlayer(&ad->players, aip);
            removed = 1;
            break;
        }
    }
    
    pthread_mutex_unlock(&ad->mutex);
    
    if (removed == 1)
        chat->SendMessage(p, "%s removed from active ai bots.", target_player->name);
    else
        chat->SendMessage(p, "Target is not an ai bot.");
}

/*****************************/

local int GetInterfaces(Imodman *mm_) {
    mm = mm_;

    fake = mm->GetInterface(I_FAKE, ALLARENAS);
    lm = mm->GetInterface(I_LOGMAN, ALLARENAS);
    aman = mm->GetInterface(I_ARENAMAN, ALLARENAS);
    chat = mm->GetInterface(I_CHAT, ALLARENAS);
    cmd = mm->GetInterface(I_CMDMAN, ALLARENAS);
    ml = mm->GetInterface(I_MAINLOOP, ALLARENAS);
    game = mm->GetInterface(I_GAME, ALLARENAS);
    pd = mm->GetInterface(I_PLAYERDATA, ALLARENAS);
    config = mm->GetInterface(I_CONFIG, ALLARENAS);
    map = mm->GetInterface(I_MAPDATA, ALLARENAS);
    prng = mm->GetInterface(I_PRNG, ALLARENAS);
    net = mm->GetInterface(I_NET, ALLARENAS);

    if (!(fake && lm && aman && chat && cmd && ml && game && pd && config && map && prng && net))
        mm = NULL;
    return mm != NULL;
}

local void ReleaseInterfaces(Imodman* mm_) {
    mm_->ReleaseInterface(net);
    mm_->ReleaseInterface(prng);
    mm_->ReleaseInterface(map);
    mm_->ReleaseInterface(config);
    mm_->ReleaseInterface(pd);
    mm_->ReleaseInterface(game);
    mm_->ReleaseInterface(ml);
    mm_->ReleaseInterface(cmd);
    mm_->ReleaseInterface(chat);
    mm_->ReleaseInterface(aman);
    mm_->ReleaseInterface(lm);
    mm_->ReleaseInterface(fake);
    mm = NULL;
}

local Iai myai =
{
    INTERFACE_HEAD_INIT(I_AI, "ai")
    CreateAI, DestroyAI, Lock, Unlock
};

EXPORT const char info_ai[] = "ai v0.1 by monkey\n";
EXPORT int MM_ai(int action, Imodman *mm_, Arena* arena) {
    int rv = MM_FAIL;

    switch (action) {
        case MM_LOAD:
        {
            if (!GetInterfaces(mm_)) {
                ReleaseInterfaces(mm_);
                break;
            }

            adkey = aman->AllocateArenaData(sizeof(AIArenaData));
            if (adkey == -1) {
                ReleaseInterfaces(mm_);
                break;
            }
            
            mm->RegInterface(&myai, ALLARENAS);

            rv = MM_OK;
        }
        break;
        case MM_UNLOAD:
        {
            if (mm->UnregInterface(&myai, ALLARENAS))
                break;
            aman->FreeArenaData(adkey);
            ReleaseInterfaces(mm_);
            rv = MM_OK;
        }
        break;
        case MM_ATTACH:
        {
            AIArenaData *ad = P_ARENA_DATA(arena, adkey);
            
            pthread_mutexattr_init(&ad->pthread_attr);
            pthread_mutexattr_settype(&ad->pthread_attr, PTHREAD_MUTEX_RECURSIVE);
            
            if (pthread_mutex_init(&ad->mutex, &ad->pthread_attr) != 0) {
                lm->Log(L_ERROR, "<%s> Failed to create pthread mutex.", MODULE_NAME);
                break;
            }
            
            ReadConfig(arena);
            
            ad->last_update = current_ticks();

            LLInit(&ad->players);
            LLInit(&ad->weapons);
            LLInit(&ad->weapons_destroy);
            
            ml->SetTimer(UpdateTimer, 25, 25, arena, NULL);

            cmd->AddCommand("createai", Ccreateai, arena, help_createai);
            cmd->AddCommand("removeai", Cremoveai, arena, help_removeai);

            mm->RegCallback(CB_PPK, OnPPK, arena);
            mm->RegCallback(CB_ARENAACTION, OnArenaAction, arena);

            rv = MM_OK;
        }
        break;
        case MM_DETACH:
        {
            AIArenaData *ad = P_ARENA_DATA(arena, adkey);

            cmd->RemoveCommand("createai", Ccreateai, arena);
            cmd->RemoveCommand("removeai", Cremoveai, arena);

            mm->UnregCallback(CB_PPK, OnPPK, arena);
            mm->UnregCallback(CB_ARENAACTION, OnArenaAction, arena);

            ml->ClearTimer(UpdateTimer, NULL);

            AIPlayer* aip = LLRemoveFirst(&ad->players);
            while (aip) {
                fake->EndFaked(aip->player);
                afree(aip);
                aip = LLRemoveFirst(&ad->players);
            }
            LLEmpty(&ad->players);
            
            LLEmpty(&ad->weapons_destroy);
            
            EnemyWeapon* weapon= LLRemoveFirst(&ad->weapons);
            while (weapon) {
                afree(weapon);
                weapon = LLRemoveFirst(&ad->weapons);
            }
            LLEmpty(&ad->weapons);
            
            
            pthread_mutexattr_destroy(&ad->pthread_attr);
            pthread_mutex_destroy(&ad->mutex);

            rv = MM_OK;
        }
        break;
    }

    return rv;
}
