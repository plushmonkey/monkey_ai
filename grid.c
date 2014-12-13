#include "grid.h"
#include <stdlib.h>

// Used as offsets for grid functions.
static Node directions[8] = {
    { 0, -1 }, { 1, 0 }, { 0, 1 }, { -1, 0 },
    { -1, -1 }, { 1, -1 }, { -1, 1 }, { 1, 1 }
};

// Allocate memory and set defaults for the grid.
void grid_initialize(Grid *grid, short width, short height) {
    int x, y;
    
    grid->nodes = malloc(sizeof(Node) * width * height);
    grid->width = width;
    grid->height = height;
    
    for (y = 0; y < height; ++y) {
        for (x = 0; x < width; ++x) {
            grid->nodes[y * height + x].x = x;
            grid->nodes[y * height + x].y = y;
            grid->nodes[y * height + x].near_wall = FALSE;
            grid->nodes[y * height + x].solid = FALSE;
        }
    }
}

void grid_free(Grid *grid) {
    free(grid->nodes);
}

Node* grid_get_node(Grid *grid, short x, short y) {
    return &grid->nodes[y * grid->height + x];
}

BOOL grid_is_solid(Grid *grid, short x, short y) {
    return grid_get_node(grid, x, y)->solid;
}

BOOL grid_is_valid(Grid *grid, short x, short y) {
    return x >= 0 && x < grid->width && y >= 0 && y < grid->height;
}

BOOL grid_is_open(Grid *grid, short x, short y) {
    return grid_is_valid(grid, x, y) && 
          !grid_is_solid(grid, x, y) && 
          !grid_get_node(grid, x, y)->near_wall;
}

void grid_set_solid(Grid *grid, short x, short y, BOOL solid) {
    int i;
    
    grid_get_node(grid, x, y)->solid = solid;
    
    if (solid) {
        for (i = 0; i < 8; ++i) {
            short nx = x + directions[i].x;
            short ny = y + directions[i].y;
            
            if (grid_is_valid(grid, nx, ny))
                grid_get_node(grid, nx, ny)->near_wall = TRUE;
        }
    }
}

// Gets only the valid/open neighbors.
NodeNeighbors grid_get_neighbors(Grid *grid, Node *node) {
    NodeNeighbors neighbors;
    int i;
    
    neighbors.count = 0;
    
    for (i = 0; i < 8; ++i) {
        int nx = node->x + directions[i].x;
        int ny = node->y + directions[i].y;
        
        neighbors.neighbors[i] = NULL;
        
        if (grid_is_open(grid, nx, ny))
            neighbors.neighbors[neighbors.count++] = grid_get_node(grid, nx, ny);
    }
    
    return neighbors;
}
