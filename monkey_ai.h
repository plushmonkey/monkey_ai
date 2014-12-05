#ifndef MONKEY_AI_H_
#define MONKEY_AI_H_

#include "asss.h"

struct EnemyWeapon;                                  
typedef int (*WeaponUpdate)(struct EnemyWeapon *, int);

/** The data that's associated with each ai player. */
typedef struct {
    /** The name of this ai player. */
    char name[24];
    
    /** The current ship of this ai player. */
    byte ship;

    /** The fake player struct for this ai player. */
    Player *player;

    /** The player that this ai player is targeting. */
    Player *target;

    /** The x position in pixels. */
    double x;

    /** The y position in pixels. */
    double y;
    
    /** The frequency that this ai player is on. */
    int freq;

    /** The current energy of this ai player. */
    double energy;
    
    /** The current recharge of this ai player. */
    double recharge;
    
    /** The current xspeed of this ai player. */
    double xspeed;
    
    /** The current yspeed of this ai player. */
    double yspeed;
    
    /** The current rotation of this ai player. */
    double rotation;
    
    /** 1 if dead, 0 if alive. */
    int dead;
    
    /** The tick when this ai player died. */
    int time_died;
    
    /** The last player to hit this ai player. */
    Player *last_hitter;
    
    /** The last weapon that hit the ai player. */
    struct EnemyWeapon *last_weapon;
} AIPlayer;

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

/** This callback happens when an AI player takes damage. */
#define CB_AIDAMAGE "aidamage"
/**
 * @param aip The AI player that took damage.
 * @param weapon The weapon that did the damage.
 */
typedef void (*AIDamageFunc)(AIPlayer *aip, EnemyWeapon *weapon);

/** This callback happens when a new AI player is created. */
#define CB_NEWAIPLAYER "newaiplayer"
/**
 * @param aip The AI player that was created.
 */
typedef void (*NewAIFunc)(AIPlayer *aip);

/** This callback happens when an AI player is killed. */
#define CB_AIKILL "aikill"
/**
 * @param killer The asss player that killed the AI player.
 * @param killed The AI player that was killed.
 * @param weapon The weapon that was used to kill the AI player.
 */
typedef void (*AIKillFunc)(Player *killer, AIPlayer *killed, EnemyWeapon *weapon);

#define I_AI "ai-1"

/** Interface used to control AI players. */
typedef struct Iai {
    INTERFACE_HEAD_DECL
    
    /** Creates a new AI player.
     * @param arena The arena where the AI player will be created.
     * @param name The name of the new AI player. Defaults to "*ai*" if empty.
     * @param freq The frequency to put the new AI player on.
     * @param ship The ship of the new AI player.
     * @return The new AIPlayer. NULL if an error occured.
     */
    AIPlayer* (*CreateAI)(Arena *arena, const char *name, int freq, int ship);
    
    /** Destroys an AI player.
     * @param aip The AI player to destroy.
     */
    void (*DestroyAI)(AIPlayer *aip);
    
    /** Locks the arena mutex. */
    void (*Lock)(Arena *arena);
    
    /** Unlocks the arena mutex. */
    void (*Unlock)(Arena *arena);
} Iai;

#endif
