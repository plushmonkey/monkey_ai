#ifndef MONKEY_PATH_H_
#define MONKEY_PATH_H_

#include "asss.h"
#include "grid.h"

#define I_PATHING "pathing-1"

/** Interface used for pathfinding. */
typedef struct Ipathing {
    INTERFACE_HEAD_DECL
    
    /** Get direct access to the pathing grid.
      * @param arena The arena
      * @return the grid of the arena
    */
    Grid* (*GetGrid)(Arena *arena);
    
    /** Find a path from one point to another.
     * @param arena The arena to search in
     * @param startX The starting x position.
     * @param startY The starting y position.
     * @param endX The ending x position.
     * @param endY The ending y position.
     * @return a LinkedList which contains a path of nodes.
    */
    LinkedList* (*FindPath)(Arena *arena, short startX, short startY, short endX, short endY);
} Ipathing;

#endif
