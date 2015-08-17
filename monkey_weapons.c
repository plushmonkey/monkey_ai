#include "monkey_weapons.h"

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

#define MODULE_NAME "monkey_weapons"
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

    /** The amount of times the a bomb can bounce. */
    int bounce_count[8];
    
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
    
    /** Number of pixels from the player that are affected by a repel. */
    int repel_distance;
    
    /** How many ticks this repel is alive. */
    int repel_time;
} ArenaConfig;

/** The data that's associated with each arena. */
typedef struct {
    /** The list of active weapons in this arena. */
    LinkedList weapons;
    
    /** The list of active weapons to be destroyed next tick. */
    LinkedList weapons_destroy;

    /** The configuration settings for this arena. */
    ArenaConfig config;

    /** Last time the weapons were updated. */
    int last_update;
    
    /** The mutex to lock when accessing any arena data. */
    pthread_mutex_t mutex;
    
    /** The mutex attributes. Recursive mutex. */
    pthread_mutexattr_t pthread_attr;
} WeaponsArenaData;
local int adkey;


local void ReadConfig(Arena* arena);
local int InSafe(Arena *arena, int x, int y);

/************************/

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
    WeaponsArenaData *ad = P_ARENA_DATA(arena, adkey);
    
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

/** Does damage to all of the ai players near a bomb.
 * Arena mutex should always be locked before calling this.
 * @param arena The arena where the collision happened.
 * @param weapon The weapon that collided.
 */
local void DoBombDamage(Arena *arena, EnemyWeapon *weapon) {
    WeaponsArenaData *ad = P_ARENA_DATA(arena, adkey);
    
    int radius = ad->config.bomb_explode_pixels + ad->config.bomb_explode_pixels * weapon->level;
    
    if (weapon->type == W_PROXBOMB) {
        int prox_dist = ad->config.proximity_distance + weapon->level;
        radius += prox_dist * 16;
    }
    
    Player *p;
    Link *link;
    
    DO_CBS(CB_BOMBEXPLOSION, arena, BombExplosionFunc, (weapon));
    
    pd->Lock();
    FOR_EACH_PLAYER_IN_ARENA(p, arena) {   
        if (p->p_ship == SHIP_SPEC || p->p_freq == weapon->shooter->p_freq) continue;
        
        int dx = p->position.x - weapon->x;
        int dy = p->position.y - weapon->y;
        
        double dist = sqrt(dx * dx + dy * dy);
        
        if (dist <= radius + 4)
            DO_CBS(CB_WEAPONHIT, arena, WeaponHitFunc, (p, weapon));
    }
    pd->Unlock();
}

/** Checks for weapon/player collisions.
 * @param player The player to check
 * @param weapon The weapon to check
 * @return 1 if the weapon hit the player, 0 otherwise
 */
local int CheckWeaponHit(Player *player, EnemyWeapon *weapon) {
    if (weapon->active == 0 || weapon->destroy == 1) return 0;
    if (weapon->shooter->p_freq == player->p_freq) return 0;
    if (weapon->shooter == player) return 0;
    if (weapon->type == W_REPEL) return 0;
    
    WeaponsArenaData *ad = P_ARENA_DATA(player->arena, adkey);

    pthread_mutex_lock(&ad->mutex);
    
    // Used for bomb calculations
    double ed = ad->config.bomb_explode_delay;
    
    int ship_radius = ad->config.radius[player->p_ship];
    int hit_dist = ship_radius * 2;
    
    if (weapon->type == W_PROXBOMB) {
        int prox_dist = ad->config.proximity_distance + weapon->level;
        hit_dist = (ship_radius * 2) + prox_dist * 16;
    }
    
    struct PlayerPosition *pos = &player->position;
    
    double x = pos->x;
    double y = pos->y;
    
    if (weapon->type == W_BOMB || weapon->type == W_PROXBOMB) {
        x += (pos->xspeed / 10) * (UPDATE_FREQUENCY / 100.0);
        y += (pos->yspeed / 10) * (UPDATE_FREQUENCY / 100.0);
    }
    
    double dx = weapon->x - x;
    double dy = weapon->y - y;

    double dist = sqrt(dx * dx + dy * dy);
    int rv = 0;
    
    if (dist <= hit_dist) {
        if (weapon->type == W_PROXBOMB) {
            // Move weapon position and player position by the bomb explode delay then calculate damage.
            weapon->x += cos(weapon->rotation) * (ed / 100.0) + ((weapon->xspeed / 10) * (ed / 100.0));
            weapon->y -= sin(weapon->rotation) * (ed / 100.0) - ((weapon->yspeed / 10) * (ed / 100.0));
           
            DoBombDamage(player->arena, weapon);
        } else if (weapon->type == W_BOMB) {
            DoBombDamage(player->arena, weapon);
        } else {
            DO_CBS(CB_WEAPONHIT, player->arena, WeaponHitFunc, (player, weapon));
        }

        rv = 1;
    }
    
    pthread_mutex_unlock(&ad->mutex);
    return rv;
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

/** Traces along the weapon's path
 * @param weapon The weapon is that is being traced.
 * @param dt The timestep.
 * @return Returns 1 if wall collision happened, 0 otherwise.
 */
local int TraceWeapon(EnemyWeapon *weapon, int dt) {
    Arena *arena = weapon->arena;
    WeaponsArenaData *ad = P_ARENA_DATA(arena, adkey);

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
                if ((weapon->bouncing && weapon->bounces_left-- <= 0) || !weapon->bouncing) {
                    DoBombDamage(arena, weapon);
                    break;
                }
            }
            
            // if bouncing then flip the rotation of the weapon when it collides with a tile
            if (weapon->bouncing) {
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
                
                if (horizontal) {
                    s = -s;
                    weapon->yspeed *= -1;
                }
                    
                if (vertical) {
                    c = -c;
                    weapon->xspeed *= -1;
                }
                
                weapon->rotation = atan2(s, c);
                
                solid = 0;
            } else {
                break;
            }
        }

        last_tile_x = tile_x;
        last_tile_y = tile_y;
        
        Link *link;
        Player *player;
        
        pd->Lock();
        FOR_EACH_PLAYER_IN_ARENA(player, arena) {
            if (player->p_ship != SHIP_SPEC && 
                player->p_freq != weapon->shooter->p_freq)
            {
                weapon->x = x;
                weapon->y = y;
                
                if (CheckWeaponHit(player, weapon)) {
                    pd->Unlock();
                    pthread_mutex_unlock(&ad->mutex);
                    return 1;
                }
            }
        }
        pd->Unlock();
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
local int UpdateRepel(EnemyWeapon *weapon, int dt) {
    WeaponsArenaData *ad = P_ARENA_DATA(weapon->arena, adkey);

    int ticks = current_ticks();
    
    pthread_mutex_lock(&ad->mutex);
    if (ticks - weapon->created >= UPDATE_FREQUENCY + ad->config.repel_time) {
        pthread_mutex_unlock(&ad->mutex);
        return 1;   // It will be marked as destroyed in the calling function
    }
    
    pthread_mutex_unlock(&ad->mutex);
    return 0;
}


/** Update weapons by a single tick.
 * @param arena The arena to update.
 */
local void DoTick(Arena *arena) {
    WeaponsArenaData *ad = P_ARENA_DATA(arena, adkey);
    
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
            FlagWeaponForDestroy(arena, weapon);
            continue;
        }
    }
    
    // Remove weapons that are flagged to be destroyed
    FOR_EACH(&ad->weapons_destroy, weapon, link)
        DestroyWeapon(&ad->weapons, weapon);
    LLEmpty(&ad->weapons_destroy);
    
    pthread_mutex_unlock(&ad->mutex);
}


/*****************************/

/** Timer to update the weapons.
 * @param param The arena to update.
 */
local int UpdateTimer(void *param) {
    Arena *arena = param;
    WeaponsArenaData *ad = P_ARENA_DATA(arena, adkey);

    int ticks = current_ticks();
    
    pthread_mutex_lock(&ad->mutex);
    
    int dt = ticks - ad->last_update;

    // Update players and weapons by 1 tick at a time
    for (int i = 0; i < dt; ++i)
        DoTick(arena);
        
    ad->last_update = current_ticks();

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
    WeaponsArenaData *ad = P_ARENA_DATA(arena, adkey);
    
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
        weapon->update = UpdateRepel;
        weapon->shooter = p;
        weapon->active = 1;
        weapon->level = 0;
        
        LLAdd(&ad->weapons, weapon);
        
        pthread_mutex_unlock(&ad->mutex);
        
        DO_CBS(CB_WEAPONCREATED, arena, WeaponCreatedFunc, (weapon));
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
            weapon->update = TraceWeapon;
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
            
            DO_CBS(CB_WEAPONCREATED, arena, WeaponCreatedFunc, (weapon));
        }
        
        pthread_mutex_unlock(&ad->mutex);
        return;
    }
          
    EnemyWeapon *weapon = amalloc(sizeof(EnemyWeapon));
    
    weapon->rotation = ((40 - (pos->rotation + 30) % 40) * 9) * (M_PI / 180);
    
    int radius = ad->config.radius[p->p_ship];
    
    
    weapon->xspeed = p->position.xspeed;
    weapon->yspeed = p->position.yspeed;
    weapon->type = pos->weapon.type;
    weapon->destroy = 0;
    weapon->level = pos->weapon.level;
    
    weapon->speed = ad->config.bullet_speed[p->p_ship];
    
    if (weapon->type == W_BOMB || weapon->type == W_PROXBOMB) {
        weapon->bounces_left = ad->config.bounce_count[p->p_ship];
        weapon->max_damage = ad->config.bomb_damage_level;
        weapon->bouncing = weapon->bounces_left > 0;
        
        weapon->speed = ad->config.bomb_speed[p->p_ship];
        weapon->x = pos->x + radius * cos(weapon->rotation);
        weapon->y = pos->y - radius * sin(weapon->rotation);;
    }
    
    if (weapon->type == W_BULLET || weapon->type == W_BOUNCEBULLET) {
        int level = pos->weapon.level;
        weapon->max_damage = ad->config.bullet_damage_level + ad->config.bullet_damage_upgrade * level;
        
        if (ad->config.bullet_exact_damage == 0)
            weapon->max_damage = prng->Number(1, weapon->max_damage);
        weapon->x = pos->x + radius * cos(weapon->rotation);
        weapon->y = pos->y - radius * sin(weapon->rotation);
        weapon->bouncing = pos->weapon.type == W_BOUNCEBULLET;
    }
    
    weapon->created = current_ticks();
    weapon->arena = arena;
    weapon->parent = NULL;
    weapon->update = TraceWeapon;
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
            
            DO_CBS(CB_WEAPONCREATED, arena, WeaponCreatedFunc, (first));
            DO_CBS(CB_WEAPONCREATED, arena, WeaponCreatedFunc, (second));
        }
    }
    
    DO_CBS(CB_WEAPONCREATED, arena, WeaponCreatedFunc, (weapon));
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
    WeaponsArenaData *ad = P_ARENA_DATA(arena, adkey);
    
    pthread_mutex_lock(&ad->mutex);
    
    for (int i = 0; i < 8; ++i) {
        ad->config.radius[i] = config->GetInt(arena->cfg, ShipNames[i], "Radius", 14);
        ad->config.bullet_speed[i] = config->GetInt(arena->cfg, ShipNames[i], "BulletSpeed", 2000);
        ad->config.bomb_speed[i] = config->GetInt(arena->cfg, ShipNames[i], "BombSpeed", 2000);
        
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
    ad->config.repel_time = config->GetInt(arena->cfg, "Repel", "RepelTime", 225);
    
    ad->config.burst_damage_level = config->GetInt(arena->cfg, "Burst", "BurstDamageLevel", 700);
    
    pthread_mutex_unlock(&ad->mutex);
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

EXPORT const char info_weapons[] = "weapons v1.0 by monkey\n";
EXPORT int MM_weapons(int action, Imodman *mm_, Arena* arena) {
    int rv = MM_FAIL;

    switch (action) {
        case MM_LOAD:
        {
            if (!GetInterfaces(mm_)) {
                ReleaseInterfaces(mm_);
                break;
            }

            adkey = aman->AllocateArenaData(sizeof(WeaponsArenaData));
            if (adkey == -1) {
                ReleaseInterfaces(mm_);
                break;
            }
            
            rv = MM_OK;
        }
        break;
        case MM_UNLOAD:
        {
            aman->FreeArenaData(adkey);
            ReleaseInterfaces(mm_);
            rv = MM_OK;
        }
        break;
        case MM_ATTACH:
        {
            WeaponsArenaData *ad = P_ARENA_DATA(arena, adkey);
            
            pthread_mutexattr_init(&ad->pthread_attr);
            pthread_mutexattr_settype(&ad->pthread_attr, PTHREAD_MUTEX_RECURSIVE);
            
            if (pthread_mutex_init(&ad->mutex, &ad->pthread_attr) != 0) {
                lm->LogA(L_ERROR, MODULE_NAME, arena, "Failed to create pthread mutex.");
                break;
            }
            
            ReadConfig(arena);
            
            ad->last_update = current_ticks();

            LLInit(&ad->weapons);
            LLInit(&ad->weapons_destroy);
            
            ml->SetTimer(UpdateTimer, UPDATE_FREQUENCY, UPDATE_FREQUENCY, arena, NULL);

            mm->RegCallback(CB_PPK, OnPPK, arena);
            mm->RegCallback(CB_ARENAACTION, OnArenaAction, arena);

            rv = MM_OK;
        }
        break;
        case MM_DETACH:
        {
            WeaponsArenaData *ad = P_ARENA_DATA(arena, adkey);

            mm->UnregCallback(CB_PPK, OnPPK, arena);
            mm->UnregCallback(CB_ARENAACTION, OnArenaAction, arena);

            ml->ClearTimer(UpdateTimer, NULL);

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
