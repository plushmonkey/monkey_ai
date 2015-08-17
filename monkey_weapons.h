#ifndef MONKEY_WEAPONS_H_
#define MONKEY_WEAPONS_H_

#include "asss.h"

struct EnemyWeapon;           
                       
/** Function that is called to update a weapon */
typedef int (*WeaponUpdate)(struct EnemyWeapon *, int);

/** The data that's associated with each active weapon. */
typedef struct EnemyWeapon {
    /** The arena that this weapon is active in. */
    Arena *arena;

    /** The x position of this weapon in pixels. */
    double x;

    /** The y position of this weapon in pixels. */
    double y;
    
    /** How fast the weapon is traveling in the x direction. */
    double xspeed;
    
    /** How fast the weapon is traveling in the y direction. */
    double yspeed;

    /** The direction this weapon is traveling. */
    double rotation;

    /** The speed of the weapon in pixels / second * 10. */
    int speed;

    /** When this weapon was created in ticks. */
    int created;

    /** 1 if the weapon is bouncing, 0 otherwise. */
    int bouncing;

    /** Number of bounces left (bombs) */
    int bounces_left;
    
    /** The weapon type. Values are from ppk.h */
    int type;
    
    /** The weapon level. */
    int level;
    
    /** The maximum amount of damage this weapon can cause. */
    int max_damage;
    
    /** The parent weapon of this weapon. Used so only 1 bullet of the set hits. */
    struct EnemyWeapon *parent;
    
    /** Marks the weapon to be destroyed next tick. */
    int destroy;
    
    /** The update function to call every tick.
     * The weapon is marked as destroy if it returns 1.
     */
    WeaponUpdate update;
    
    /** The player that shot this weapon. */
    Player *shooter;
    
    /** 1 if the weapon is active, 0 otherwise. */
    int active;
} EnemyWeapon;

#define CB_WEAPONHIT "weaponhit"
typedef void (*WeaponHitFunc)(Player *player, EnemyWeapon *weapon);

#define CB_BOMBEXPLOSION "bombexplosion"
typedef void (*BombExplosionFunc)(EnemyWeapon *weapon);

#define CB_WEAPONCREATED "weaponcreated"
typedef void (*WeaponCreatedFunc)(EnemyWeapon *weapon);

#endif
