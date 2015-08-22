#include "monkey_ai.h"

#include "asss.h"
#include "fake.h"
#include "packets/kill.h"
#include "monkey_pathing.h"

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
local Ipathing *path;

#define MODULE_NAME "monkey_ai"
#define UPDATE_FREQUENCY 25

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
    
    /** The initial speed for each ship. */
    int initial_speed[8];
    
    /** The maximum speed for each ship. */
    int max_speed[8];

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
    
    /** How long in ticks after the prox sensor is triggered before bomb explodes */
    int bomb_explode_delay;
    
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

typedef enum StateType {
    ChaseState,
    BombState
} StateType;

struct State;

typedef void(*StateUpdate)(AIPlayer *aip, struct State *state);

typedef struct State {
    void *data;
    StateType type;
    StateUpdate update;
} State;

typedef struct StateMachine {
    LinkedList stack;
} StateMachine;

local void ReadConfig(Arena* arena);
local void DestroyAIPlayer(LinkedList *players, AIPlayer *aip);
local void GetSpawnPoint(Arena *arena, int freq, int *spawnx, int *spawny);
local int InSafe(Arena *arena, int x, int y);
local int BulletDamage(AIPlayer *aip, EnemyWeapon *weapon);
local int BombDamage(AIPlayer *aip, EnemyWeapon *weapon);
local int RepelDamage(AIPlayer *aip, EnemyWeapon *weapon);

local void PushState(StateMachine *machine, State *state) {
    LLAddFirst(&machine->stack, state);
}

local State *PopState(StateMachine *machine) {
    return LLRemoveFirst(&machine->stack);
}

local State *GetState(StateMachine *machine) {
    return LLGetHead(&machine->stack)->data;
}

local void ChaseUpdate(AIPlayer *aip, State *state) {
    
}

local void BombUpdate(AIPlayer *aip, State *state) {
    lm->Log(L_INFO, "Bombing.");
}

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
local void SetDamageFunction(AIPlayer *aip, short weapon_type, WeaponDamageFunc func) {
    Lock(aip->player->arena);
    aip->damage_funcs[weapon_type] = func;
    Unlock(aip->player->arena);
}

/* Defined in interface */
local AIPlayer *CreateAI(Arena *arena, const char *name, int freq, int ship) {
    AIArenaData *ad = P_ARENA_DATA(arena, adkey);
    AIPlayer* aip = amalloc(sizeof(AIPlayer));

    if (*name) 
        strncpy(aip->name, name, 24);
    else 
        strncpy(aip->name, "*ai*", 24);
    
    int x, y;
    GetSpawnPoint(arena, freq, &x, &y);
    
    aip->path = LLAlloc();
    aip->last_pathing = 0;
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
    aip->damage_funcs[W_BULLET] = aip->damage_funcs[W_BOUNCEBULLET] = BulletDamage;
    aip->damage_funcs[W_BOMB] = aip->damage_funcs[W_PROXBOMB] = BombDamage;
    aip->damage_funcs[W_REPEL] = RepelDamage;
    aip->damage_funcs[W_BURST] = BulletDamage;
    
    aip->state_machine = malloc(sizeof(StateMachine));
    State* state = malloc(sizeof(State));
    
    state->update = ChaseUpdate;
    state->type = ChaseState;
    state->data = NULL;
    
    PushState(aip->state_machine, state);
    
    pthread_mutex_lock(&ad->mutex);
    aip->energy = ad->config.initial_energy[aip->ship];
    aip->recharge = ad->config.initial_recharge[aip->ship];
    aip->target.type = TargetNone;
    
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
        if (player->flags.is_dead || InSafe(arena, player->position.x / 16, player->position.y / 16)) continue;
        
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

/** Calculate the weapon damage when hit by a bullet/burst
 * @param aip The AI player that is being hit.
 * @param weapon The weapon that hit the AI player.
 * @return the amount of damage to deal.
 */
local int BulletDamage(AIPlayer *aip, EnemyWeapon *weapon) {
    return weapon->max_damage;
}

/** Calculate the weapon damage when hit by a bomb/prox.
 * @param aip The AI player that is being hit.
 * @param weapon The weapon that hit the AI player.
 * @return the amount of damage to deal.
 */
local int BombDamage(AIPlayer *aip, EnemyWeapon *weapon) {
    Arena *arena = aip->player->arena;
    if (InSafe(arena, aip->x / 16, aip->y / 16))
        return 0;
        
    AIArenaData *ad = P_ARENA_DATA(arena, adkey);
    int radius = ad->config.bomb_explode_pixels + ad->config.bomb_explode_pixels * weapon->level;
    
    int dx = aip->x - weapon->x;
    int dy = aip->y - weapon->y;
    
    double dist = sqrt(dx * dx + dy * dy);
    
    int damage = 0;
    if (dist < radius)
        damage = floor(((radius - dist) / radius) * ad->config.bomb_damage_level);
    
    lm->Log(L_INFO, "Doing %d bomb damage to %s.", damage, aip->name);
    return damage;
}

/** Calculate the weapon damage when hit by a repel.
 * @param aip The AI player that is being hit.
 * @param weapon The weapon that hit the AI player.
 * @return the amount of damage to deal.
 */
local int RepelDamage(AIPlayer *aip, EnemyWeapon *weapon) {
    return 0;
}

/** Determines if the tile x, y is solid.
 * @param arena The current arena.
 * @param x The x tile to check.
 * @param y The y tile to check.
 * @return Returns 1 if it is solid, 0 otherwise.
 */
local int IsSolid(Arena *arena, int x, int y) {
    enum map_tile_t type = map->GetTile(arena, x, y);

    return !(type == TILE_NONE || 
             type == TILE_SAFE ||
             type == TILE_TURF_FLAG ||
             type == TILE_GOAL ||
             type == 241 ||
             type >= 252 ||
            (type >= TILE_OVER_START && type <= TILE_UNDER_END + 1));
}
/** Determines if the tile x, y is in a safe zone.
 * @param arena The current arena.
 * @param x The x tile to check.
 * @param y The y tile to check.
 * @return Returns 1 if it is a safe tile, 0 otherwise.
 */
local int InSafe(Arena *arena, int x, int y) {
    return map->GetTile(arena, x, y) == TILE_SAFE;
}

/** Pushes any ai players inside the repel radius.
 * @param weapon The repel weapon that is being updated.
 * @param dt The timestep.
 * @return Returns 1 if the repel has timed out, 0 otherwise.
 */
local int UpdateRepel(EnemyWeapon *weapon, int dt) {
    AIArenaData *ad = P_ARENA_DATA(weapon->arena, adkey);

    int ticks = current_ticks();
    
    pthread_mutex_lock(&ad->mutex);
    if (ticks - weapon->created >= UPDATE_FREQUENCY + ad->config.repel_time) {
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
    
    pthread_mutex_unlock(&ad->mutex);
    return 0;
}

/** Update ai players by a single tick.
 * @param arena The arena to update.
 */
local void DoTick(Arena *arena) {
    AIArenaData *ad = P_ARENA_DATA(arena, adkey);

    pthread_mutex_lock(&ad->mutex);  
    
    AIPlayer *aip;
    Link *link;
    // Update each player 1 tick
    FOR_EACH(&ad->players, aip, link) {
        const int thrust = ad->config.initial_thrust[aip->ship] * 100;
        const int Speed = ad->config.max_speed[aip->ship] / 10;
        //const int Speed = 250; // pixels per second
        
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
        
        if (aip->target.type != TargetNone) {
            int tarx, tary;
            
            if (aip->target.type == TargetPlayer) {
                tarx = aip->target.player->position.x;
                tary = aip->target.player->position.y;
            } else {
                tarx = aip->target.position.x;
                tary = aip->target.position.y;
            }
            int dx = tarx - aip->x;
            int dy = tary - aip->y;

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
                // Bounce off of the tile
                int last_tile_x = floor(aip->x / 16);
                int last_tile_y = floor(aip->y / 16);
                int tile_x = floor(aip->x + xinc) / 16;
                int tile_y = floor(aip->y + yinc) / 16;
                
                int tiledx = tile_x - last_tile_x;
                int tiledy = tile_y - last_tile_y;
                
                double x = aip->x + xinc;
                double y = aip->y + yinc;
                
                int below = (int)(floor(y)) % 16 < 3;
                int above = (int)(floor(y)) % 16 > 13;
                int right = (int)(floor(x)) % 16 < 3;
                int left = (int)(floor(x)) % 16 > 13;
                
                int horizontal = (below && tiledy > 0) || (above && tiledy < 0);
                int vertical = (right && tiledx > 0) || (left && tiledx < 0);
                
                const double BounceFactor = -0.6;
                
                if (horizontal) {
                    yinc *= -1;
                    aip->yspeed *= BounceFactor;
                }
                
                if (vertical) {
                    xinc *= -1;
                    aip->xspeed *= BounceFactor;
                }
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
    }
    
    pthread_mutex_unlock(&ad->mutex);
}

local int NearOther(int x, int y, int x2, int y2, int r) {
    int dx = x - x2;
    int dy = y - y2;
    return sqrt(dx * dx + dy * dy) <= r;
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

    // Update ai players by 1 tick at a time
    for (int i = 0; i < dt; ++i)
        DoTick(arena);
        
    ad->last_update = current_ticks();
    
    ppk.type = C2S_POSITION;
    ppk.rotation = 0;
    ppk.weapon.type = W_BOUNCEBULLET;
    ppk.weapon.level = 1;
    ppk.weapon.alternate = 0;
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
        
        if (aip->target.type != TargetPlayer && current_ticks() > aip->last_pathing + 100) {
            if (aip->path)
                LLFree(aip->path);
            aip->path = path->FindPath(arena, aip->x / 16, aip->y / 16, 545, 535);
            aip->last_pathing = current_ticks();
        }
        
        Player *tar = GetTargetPlayer(aip);
        if (tar) {
            aip->target.type = TargetPlayer;
            aip->target.player = tar;
        } else {
            if (!LLIsEmpty(aip->path)) {
                Node *head = LLGetHead(aip->path)->data;
                aip->target.type = TargetPosition;
                if (NearOther(aip->x / 16, aip->y / 16, head->x, head->y, 4)) {
                    LLRemoveFirst(aip->path);
                    if (LLIsEmpty(aip->path)) {
                        aip->target.type = TargetNone;
                        continue;
                    }
                    head = LLGetHead(aip->path)->data;
                }
                aip->target.position.x = head->x * 16;
                aip->target.position.y = head->y * 16;
                //lm->Log(L_INFO, "Target: %d, %d (%d)", head->x, head->y, LLCount(aip->path));
            } else {
                aip->target.type = TargetNone;
            }
        }
        
        State* state = GetState(aip->state_machine);
        
        state->update(aip, state);

        double angle = aip->rotation * 180 / M_PI;
        int rot = (angle / 9) + 10;
        if (rot < 0) rot += 40;
        
        ppk.rotation = rot;
        ppk.x = aip->x;
        ppk.y = aip->y;
        
        ppk.energy = aip->energy;
        ppk.xspeed = aip->xspeed;
        ppk.yspeed = aip->yspeed;
        
        if (aip->target.type != TargetPlayer || InSafe(arena, ppk.x / 16, ppk.y / 16))
            ppk.weapon.type = W_NULL;
        
        /* TODO: remove test code
        ppk.x = aip->x = 530 * 16;
        ppk.y = aip->y = 530 * 16;
        ppk.xspeed = 0;
        ppk.yspeed = 0;
        ppk.weapon.type = W_NULL;*/
        
        game->FakePosition(aip->player, &ppk, sizeof(ppk));
    }

    pthread_mutex_unlock(&ad->mutex);
    return 1;
}

/*****************************/

local void OnWeaponHit(Player *player, EnemyWeapon *weapon) {
    Arena *arena = player->arena;
    AIArenaData *ad = P_ARENA_DATA(arena, adkey);
    int type = weapon->type;

    pthread_mutex_lock(&ad->mutex);

    AIPlayer *aip;
    Link *link;

    FOR_EACH(&ad->players, aip, link) {
        if (aip->player == player) {
            aip->last_hitter = weapon->shooter;
            aip->energy -= aip->damage_funcs[type](aip, weapon);
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
        
        ad->config.initial_thrust[i] = config->GetInt(arena->cfg, ShipNames[i], "InitialThrust", 15);
        ad->config.initial_speed[i] = config->GetInt(arena->cfg, ShipNames[i], "InitialSpeed", 3200);
        ad->config.max_speed[i] = config->GetInt(arena->cfg, ShipNames[i], "MaximumSpeed", 5000);
        
        ad->config.bounce_count[i] = config->GetInt(arena->cfg, ShipNames[i], "BombBounceCount", 0);
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
    ad->config.bomb_explode_delay = config->GetInt(arena->cfg, "Bomb", "BombExplodeDelay", 2);
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
        chat->SendMessage(p, "Failed to create AI player.");
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
    path = mm->GetInterface(I_PATHING, ALLARENAS);

    if (!(fake && lm && aman && chat && cmd && ml && game && pd && config && map && prng && net && path))
        mm = NULL;
    return mm != NULL;
}

local void ReleaseInterfaces(Imodman* mm_) {
    mm_->ReleaseInterface(path);
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
    CreateAI, DestroyAI, SetDamageFunction, Lock, Unlock
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
            if (mm->UnregInterface(&myai, ALLARENAS) > 0)
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
                lm->LogA(L_ERROR, MODULE_NAME, arena, "Failed to create pthread mutex.");
                break;
            }
            
            ReadConfig(arena);
            
            ad->last_update = current_ticks();

            LLInit(&ad->players);
            
            ml->SetTimer(UpdateTimer, UPDATE_FREQUENCY, UPDATE_FREQUENCY, arena, NULL);

            cmd->AddCommand("createai", Ccreateai, arena, help_createai);
            cmd->AddCommand("removeai", Cremoveai, arena, help_removeai);

            mm->RegCallback(CB_ARENAACTION, OnArenaAction, arena);
            mm->RegCallback(CB_WEAPONHIT, OnWeaponHit, arena);

            rv = MM_OK;
        }
        break;
        case MM_DETACH:
        {
            AIArenaData *ad = P_ARENA_DATA(arena, adkey);

            cmd->RemoveCommand("createai", Ccreateai, arena);
            cmd->RemoveCommand("removeai", Cremoveai, arena);

            mm->UnregCallback(CB_ARENAACTION, OnArenaAction, arena);
            mm->UnregCallback(CB_WEAPONHIT, OnWeaponHit, arena);

            ml->ClearTimer(UpdateTimer, NULL);

            AIPlayer* aip = LLRemoveFirst(&ad->players);
            while (aip) {
                fake->EndFaked(aip->player);
                afree(aip);
                aip = LLRemoveFirst(&ad->players);
            }
            LLEmpty(&ad->players);
            
            pthread_mutexattr_destroy(&ad->pthread_attr);
            pthread_mutex_destroy(&ad->mutex);

            rv = MM_OK;
        }
        break;
    }

    return rv;
}
