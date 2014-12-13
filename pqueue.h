#ifndef PQUEUE_H_
#define PQUEUE_H_

typedef int(*PQComparator)(const void *, const void *);
typedef void* PQElement;

typedef struct PriorityQueue {
    /** The function used to determine the order of the priority queue */
    PQComparator comparator;
    
    /** The list of elements in the queue */
    PQElement *elements;
    
    /** The total number of elements in the queue + 1 */
    int num;
    
    /** The number of items allocated */
    int size;
} PriorityQueue, *PQueue;

/** Allocates a new priority queue
 * @param comp The function used to determine the order of the queue
 * @param size The initial size of the queue. It grows/shrinks when needed.
 * @return The allocated priority queue.
 */
PQueue pq_new(PQComparator comp, int size);

/** Pushes a new item into the queue
 * @param q The queue
 * @param data The data to push
 */
void pq_push(PQueue q, const void *data);

/** Pops the first item off the queue
 * @param q The queue
 * @return the data that was stored in the queue.
 */
void* pq_pop(PQueue q);

/** Returns whether or not the queue is empty.
 * @param q The queue
 * @return 1 if the queue is empty, 0 if it has items.
 */
int pq_empty(PQueue q);

#endif
