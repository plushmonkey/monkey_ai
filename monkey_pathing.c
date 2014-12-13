#include "monkey_pathing.h"

#include "asss.h"
#include "pqueue.h"
#include <math.h>
#include <stdlib.h>

local Imodman *mm;
local Ilogman *lm;
local Iarenaman *aman;
local Ichat *chat;
local Icmdman *cmd;
local Igame *game;
local Iplayerdata *pd;
local Iconfig *config;
local Imapdata *map;

typedef struct {
    Grid *grid;
} PathingArenaData;
local int adkey;

/** The function to be used in the priority queue for comparing nodes.
 * Uses the node cost and the heuristic value to determine the best node.
 * @param lhs The first node to be compared.
 * @param rhs The second node to be compared.
 * @return TRUE if the first node is better than the second node.
 */
int NodeComparator(const void *lhs, const void *rhs) {
    Node *first = (Node*)lhs;
    Node *second = (Node*)rhs;
    return first->g + first->h < second->g + second->h;
}

/** Uses manhattan distance to calculate a heuristic for finding the most optimal node.
 * Used in the jump point search algorithm.
 * @param first The first node
 * @param second The second node
 * @return The estimated cost of traveling from the first node to the second node.
 */
int ManhattanHeuristic(Node *first, Node *second) {
    return abs(first->x - second->x) + abs(first->y - second->y);
}

/** Sets default values on each node in the grid.
 * This is different from grid_initialize because it sets the pathing values.
 * @param grid The grid to reset.
 */
void ResetGrid(Grid *grid) {
    int width = grid->width;
    int height = grid->height;
    
    for (short y = 0; y < height; ++y) {
        for (short x = 0; x < width; ++x) {
            grid_get_node(grid, x, y)->closed = FALSE;
            grid_get_node(grid, x, y)->opened = FALSE;
            grid_get_node(grid, x, y)->parent = NULL;
            grid_get_node(grid, x, y)->g = 0;
            grid_get_node(grid, x, y)->h = 0;
        }
    }
}

/** Helper function to clamp a value between a min and a max.
 * @param a The value to clamp
 * @param min The minimum that the value can be
 * @param max The maximum that the value can be
 * @return the value clamped between min and max.
 */
int clamp(int a, int min, int max) {
    if (a < min) return min;
    if (a > max) return max;
    return a;
}

/** Iterative implementation of the jumping algorithm for jump point search.
 * https://harablog.wordpress.com/2011/09/07/jump-point-search/
 * @param grid The grid
 * @param goal The goal node
 * @param nx The neighbor's x value
 * @param ny The neighbor's y value
 * @param cx The node's x value
 * @param cy The node's y value
 * @return a jump point successor, or NULL if none found
 */
Node* Jump(Grid *grid, Node *goal, short nx, short ny, short cx, short cy) {
    int dx = clamp(nx - cx, -1, 1);
    int dy = clamp(ny - cy, -1, 1);
    
    if (grid_is_valid(grid, nx, ny) && grid_get_node(grid, nx, ny) == goal) 
        return grid_get_node(grid, nx, ny);
    if (!grid_is_open(grid, nx, ny)) return NULL;
    
    int offsetX = nx;
    int offsetY = ny;
    
    if (dx != 0 && dy != 0) {
        while (1) {
            // Check diagonally for forced neighbors
            if ((grid_is_open(grid, offsetX - dx, offsetY + dy) && !grid_is_open(grid, offsetX - dx, offsetY)) ||
                (grid_is_open(grid, offsetX + dx, offsetY - dy) && !grid_is_open(grid, offsetX, offsetY - dy)))
            {
                return grid_get_node(grid, offsetX, offsetY);
            }
            
            // Expand horizontally and vertically
            if (Jump(grid, goal, offsetX + dx, offsetY, offsetX, offsetY) || Jump(grid, goal, offsetX, offsetY + dy, offsetX, offsetY))
                return grid_get_node(grid, offsetX, offsetY);
            
            offsetX += dx;
            offsetY += dy;
            
            if (grid_is_valid(grid, offsetX, offsetY) && grid_get_node(grid, offsetX, offsetY) == goal) return grid_get_node(grid, offsetX, offsetY);
            if (!grid_is_open(grid, offsetX, offsetY)) return NULL;
        }
    } else {
        if (dx != 0) {
            while (1) {
                // Check horizontal forced neighbors
                if ((grid_is_open(grid, offsetX + dx, offsetY + 1) && !grid_is_open(grid, offsetX, offsetY + 1)) ||
                    (grid_is_open(grid, offsetX + dx, offsetY - 1) && !grid_is_open(grid, offsetX, offsetY - 1)))
                {
                    return grid_get_node(grid, offsetX, offsetY);
                }
                
                offsetX += dx;
                
                if (grid_is_valid(grid, offsetX, offsetY) && grid_get_node(grid, offsetX, offsetY) == goal) return grid_get_node(grid, offsetX, offsetY);
                if (!grid_is_open(grid, offsetX, offsetY)) return NULL;
            }
        } else {
            while (1) {
                // Check vertical forced neighbors
                if ((grid_is_open(grid, offsetX + 1, offsetY + dy) && !grid_is_open(grid, offsetX + 1, offsetY)) ||
                    (grid_is_open(grid, offsetX - 1, offsetY + dy) && !grid_is_open(grid, offsetX - 1, offsetY)))
                {
                    return grid_get_node(grid, offsetX, offsetY);
                }
                
                offsetY += dy;
                
                if (grid_is_valid(grid, offsetX, offsetY) && grid_get_node(grid, offsetX, offsetY) == goal) return grid_get_node(grid, offsetX, offsetY);
                if (!grid_is_open(grid, offsetX, offsetY)) return NULL;
            }
        }
    }
    return NULL;
}

/** Gets a list of neighbors of a node that need to be visited
 * @param grid The grid
 * @param node The node whose neighbors need to be found
 * @return the neighbors that weren't pruned by the jump point search pruning algorithm.
 */
NodeNeighbors FindNeighbors(Grid *grid, Node *node) {
    NodeNeighbors neighbors;
    
    neighbors.count = 0;
    
    if (!node) return neighbors;
    // Only prune if this node has a parent
    if (!node->parent) return grid_get_neighbors(grid, node);
    
    int x = node->x;
    int y = node->y;
    int px = node->parent->x;
    int py = node->parent->y;
    int dx = (x - px) / fmax(abs(x - px), 1.0f);
    int dy = (y - py) / fmax(abs(y - py), 1.0f);
    
    if (dx != 0 && dy != 0) {
        // Search diagonally
        if (grid_is_open(grid, x, y + dy))
            neighbors.neighbors[neighbors.count++] = grid_get_node(grid, x, y + dy);
        if (grid_is_open(grid, x + dx, y))
            neighbors.neighbors[neighbors.count++] = grid_get_node(grid, x + dx, y);
            
        if (grid_is_open(grid, x, y + dy) || grid_is_open(grid, x + dx, y))
            neighbors.neighbors[neighbors.count++] = grid_get_node(grid, x + dx, y + dy);
        
        if (!grid_is_open(grid, x - dx, y) && grid_is_open(grid, x, y + dy))
            neighbors.neighbors[neighbors.count++] = grid_get_node(grid, x - dx, y + dy);
            
        if (!grid_is_open(grid, x, y - dy) && grid_is_open(grid, x + dx, y))
            neighbors.neighbors[neighbors.count++] = grid_get_node(grid, x + dx, y - dy);
    } else {
        // Search horizontally and vertically
        if (dx == 0) {
            if (grid_is_open(grid, x, y + dy)) {
                neighbors.neighbors[neighbors.count++] = grid_get_node(grid, x, y + dy);
                if (!grid_is_open(grid, x + 1, y))
                    neighbors.neighbors[neighbors.count++] = grid_get_node(grid, x + 1, y + dy);
                if (!grid_is_open(grid, x - 1, y))
                    neighbors.neighbors[neighbors.count++] = grid_get_node(grid, x - 1, y + dy);
            }
        } else {
            if (grid_is_open(grid, x + dx, y)) {
                neighbors.neighbors[neighbors.count++] = grid_get_node(grid, x + dx, y);
                if (!grid_is_open(grid, x, y + 1))
                    neighbors.neighbors[neighbors.count++] = grid_get_node(grid, x + dx, y + 1);
                if (!grid_is_open(grid, x, y - 1))
                    neighbors.neighbors[neighbors.count++] = grid_get_node(grid, x + dx, y - 1);
            }
        }
    }
    return neighbors;
}

/** Find possible successor nodes and adds them to the open set
 * @param grid The grid
 * @param pq The priority queue / open set
 * @param node The current node
 * @param goal The goal node
 */
void IdentifySuccessors(Grid *grid, PQueue pq, Node *node, Node *goal) {
    NodeNeighbors neighbors = FindNeighbors(grid, node);
    
    int ng = 0;
    
    for (int i = 0; i < neighbors.count; ++i) {
        Node *neighbor = neighbors.neighbors[i];
        if (!neighbor) continue;
        
        Node *jump_point = Jump(grid, goal, neighbor->x, neighbor->y, node->x, node->y);
        
        if (jump_point) {
            if (jump_point->closed) continue;
            
            int dx = jump_point->x - node->x;
            int dy = jump_point->y - node->y;
            
            int dist = (int)sqrt(dx * dx + dy * dy);
            ng = node->g + dist;
            
            if (!jump_point->opened || ng < jump_point->g) {
                jump_point->parent = node;
                jump_point->g = ng;
                jump_point->h = ManhattanHeuristic(jump_point, goal);
                
                if (!jump_point->opened) {
                    jump_point->opened = TRUE;
                    pq_push(pq, jump_point);
                }
            }
        }
    }
}

/** Finds a path between two points using jump point search.
 * @param arena The arena to search in
 * @param startX The starting x position.
 * @param startY The starting y position.
 * @param endX The ending x position.
 * @param endY The ending y position.
 * @return a LinkedList containing the path of nodes.
 */
LinkedList* FindPath(Arena *arena, short startX, short startY, short endX, short endY) {
    PathingArenaData *ad = P_ARENA_DATA(arena, adkey);
    Grid *grid = ad->grid;
    PQueue pq = pq_new(NodeComparator, 30);
    
    ResetGrid(grid);
    
    Node *start = grid_get_node(grid, startX, startY);
    Node *goal = grid_get_node(grid, endX, endY);
    
    if (!start || !goal) return LLAlloc();
    
    start->opened = TRUE;
    
    pq_push(pq, start);
    
    LinkedList *path = LLAlloc();
    
    while (!pq_empty(pq)) {
        Node *current = pq_pop(pq);
        if (!current) continue;
        
        current->closed = TRUE;
        
        if (current == goal) {
            while (current) {
                LLAdd(path, current);
                current = current->parent;
            }
            return path;
        }
        
        IdentifySuccessors(grid, pq, current, goal);
    }
    return path;
}

/** Interface function for getting the arena grid.
 * @param arena The arena
 * @return the grid of the arena level.
 */
Grid* GetGrid(Arena *arena) {
    PathingArenaData *ad = P_ARENA_DATA(arena, adkey);
    
    return ad->grid;
}

/** Determines if a tile is solid
 * @param arena The arena
 * @param x The x tile
 * @param y The y tile
 * @return TRUE if the tile is solid, FALSE otherwise.
 */
local BOOL IsSolid(Arena *arena, int x, int y) {
    enum map_tile_t type = map->GetTile(arena, x, y);

    return !(type == TILE_NONE || 
             type == TILE_SAFE ||
             type == TILE_TURF_FLAG ||
             type == TILE_GOAL ||
            (type >= TILE_OVER_START && type <= TILE_UNDER_END));
}

/** Allocates memory for the level grid.
 * @param arena The arena
 */
local void CreateGrid(Arena *arena) {
    PathingArenaData *ad = P_ARENA_DATA(arena, adkey);
    
    ad->grid = amalloc(sizeof(Grid));
    grid_initialize(ad->grid, 1024, 1024);
    
    for (int y = 0; y < 1024; ++y) {
        for (int x = 0; x < 1024; ++x) {
            if (IsSolid(arena, x, y))
                grid_set_solid(ad->grid, x, y, TRUE);
        }
    }
}

local int GetInterfaces(Imodman *mm_) {
    mm = mm_;

    lm = mm->GetInterface(I_LOGMAN, ALLARENAS);
    aman = mm->GetInterface(I_ARENAMAN, ALLARENAS);
    chat = mm->GetInterface(I_CHAT, ALLARENAS);
    cmd = mm->GetInterface(I_CMDMAN, ALLARENAS);
    game = mm->GetInterface(I_GAME, ALLARENAS);
    pd = mm->GetInterface(I_PLAYERDATA, ALLARENAS);
    config = mm->GetInterface(I_CONFIG, ALLARENAS);
    map = mm->GetInterface(I_MAPDATA, ALLARENAS);

    if (!(lm && aman && chat && cmd && game && pd && config && map))
        mm = NULL;
        
    return mm != NULL;
}

local void ReleaseInterfaces(Imodman* mm_) {
    mm_->ReleaseInterface(map);
    mm_->ReleaseInterface(config);
    mm_->ReleaseInterface(pd);
    mm_->ReleaseInterface(game);
    mm_->ReleaseInterface(cmd);
    mm_->ReleaseInterface(chat);
    mm_->ReleaseInterface(aman);
    mm_->ReleaseInterface(lm);
    mm = NULL;
}

local Ipathing pathint = {
    INTERFACE_HEAD_INIT(I_PATHING, "pathing")
    GetGrid, FindPath
};

EXPORT const char info_pathing[] = "pathing v0.1 by monkey\n";
EXPORT int MM_pathing(int action, Imodman *mm_, Arena* arena) {
    int rv = MM_FAIL;

    switch (action) {
        case MM_LOAD:
        {
            if (!GetInterfaces(mm_)) {
                ReleaseInterfaces(mm_);
                break;
            }
            
            adkey = aman->AllocateArenaData(sizeof(PathingArenaData));
            if (adkey == -1) {
                ReleaseInterfaces(mm_);
                break;
            }
            
            mm->RegInterface(&pathint, ALLARENAS);

            rv = MM_OK;
        }
        break;
        case MM_UNLOAD:
        {
            if (mm->UnregInterface(&pathint, ALLARENAS) > 0)
                break;
            aman->FreeArenaData(adkey);
            ReleaseInterfaces(mm_);
            rv = MM_OK;
        }
        break;
        case MM_ATTACH:
        {
            CreateGrid(arena);
            
            rv = MM_OK;
        }
        break;
        case MM_DETACH:
        {
            PathingArenaData *ad = P_ARENA_DATA(arena, adkey);
            grid_free(ad->grid);
            rv = MM_OK;
        }
        break;
    }

    return rv;
}
