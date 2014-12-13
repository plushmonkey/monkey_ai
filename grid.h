#ifndef GRID_H_
#define GRID_H_

#ifndef BOOL
#define BOOL int
#endif

#ifndef TRUE
#define TRUE 1
#endif

#ifndef FALSE
#define FALSE 0
#endif

/** Represents a tile in the level graph. */
typedef struct Node {
    /** The x tile in the level. */
    short x;
    
    /** The y tile in the level. */
    short y;
    
    /** TRUE if solid, FALSE otherwise. */
    BOOL solid;
    
    /** Parent node. Used during pathfinding. */
    struct Node *parent;
    
    /** TRUE if this node was opened during pathing, FALSE otherwise. */
    BOOL opened;
    
    /** TRUE if this node was closed during pathing, FALSE otherwise. */
    BOOL closed;
    
    /** TRUE if this node is near a wall, FALSE otherwise. */
    BOOL near_wall;
    
    /** The cost of getting to this node from the starting node. */
    int g;
    
    /** The heuristic value for reaching the goal node from this node. */
    int h;
} Node;

/** A grid of nodes for pathing. */
typedef struct Grid {
    /** The list of nodes. (width * height) */
    Node *nodes;
    
    /** The width of the grid. (1024) */
    short width;
    
    /** The height of the grid. (1024) */
    short height;
} Grid;

/** A structure for storing node neighbors. */
typedef struct {
    /** A buffer to store the list of neighbors. */
    Node* neighbors[8];
    
    /** The number of neighbors. */
    int count;
} NodeNeighbors;

/** Initialize the grid. 
 * @param grid The grid to initialize.
 * @param width The width of the grid. (1024)
 * @param height The height of the grid. (1024)
 */
void grid_initialize(Grid *grid, short width, short height);

/** Free the node memory that the grid is using. 
 * @param grid The grid whose nodes should be freed.
 */
void grid_free(Grid *grid);

/** Returns a node.
 * @param grid The grid
 * @param x The x position
 * @param y The y position
 * @return the node that was specified.
 */
Node* grid_get_node(Grid *grid, short x, short y);

/** Return whether or not the grid is a solid tile.
 * @param grid The grid
 * @param x The x position
 * @param y The y position
 * @return TRUE if the grid is solid, FALSE otherwise.
 */
BOOL grid_is_solid(Grid *grid, short x, short y);

/** Return whether or not the grid is valid.
 * @param grid The grid
 * @param x The x position
 * @param y The y position
 * @return TRUE if the grid is valid, FALSE otherwise.
 */
BOOL grid_is_valid(Grid *grid, short x, short y);

/** Return whether or not that grid is an open space (not near wall)
 * @param grid The grid
 * @param x The x position
 * @param y The y position
 * @return TRUE if the grid is open, FALSE otherwise.
 */
BOOL grid_is_open(Grid *grid, short x, short y);

/** Sets the solid state of a node.
 * @param grid The grid
 * @param x The x position
 * @param y The y position
 * @param solid The solid state of the node
 */
void grid_set_solid(Grid *grid, short x, short y, BOOL solid);

/** Returns all of the neighbors of a node.
 * @param grid The grid
 * @param node The node whose neighbors should be found
 * @return the node's neighbors.
 */
NodeNeighbors grid_get_neighbors(Grid *grid, Node *node);

#endif
