#ifndef MONKEY_AI_H_
#define MONKEY_AI_H_

#include "asss.h"
#include "monkey_weapons.h"

struct AIPlayer;

/** Function that is called to do damage. 
 * Custom functions can be set per weapon type.
 * @return The amount of damage that should be dealt to the AI player.
 */
typedef int (*WeaponDamageFunc)(struct AIPlayer *, struct EnemyWeapon *);

typedef enum {
    TargetNone,
    TargetPosition,
    TargetPlayer
} TargetType;

typedef struct AITarget {
    union {
        struct {
            int x;
            int y;
        } position;
        
        Player *player;
    };
    
    TargetType type;
} AITarget;

struct StateMachine;

/** The data that's associated with each ai player. */
typedef struct AIPlayer {
    /** The name of this ai player. */
    char name[24];
    
    /** The current ship of this ai player. */
    byte ship;

    /** The fake player struct for this ai player. */
    Player *player;

    /** The player that this ai player is targeting. */
    //Player *target;
    
    /** The player's target, either a position of an asss player. */
    AITarget target;
    
    LinkedList *path;
    
    int last_pathing;

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
    
    /** The function to call per weapon type when damage should be dealt. */
    WeaponDamageFunc damage_funcs[17];
    
    struct StateMachine *state_machine;
} AIPlayer;

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
    
    /** Sets the function to call when hit by a certain weapon type.
     * @param aip The AI player
     * @param weapon_type The weapon type. W_BULLET, W_BOMB, etc
     * @param func The function that will be called.
     */
    void (*SetDamageFunction)(AIPlayer *aip, short weapon_type, WeaponDamageFunc func);
    
    /** Locks the arena mutex. */
    void (*Lock)(Arena *arena);
    
    /** Unlocks the arena mutex. */
    void (*Unlock)(Arena *arena);
} Iai;

#endif
