#include "pqueue.h"
#include <stdlib.h>

PQueue pq_new(PQComparator comp, int size) {
    PQueue q = malloc(sizeof(PriorityQueue));

    q->comparator = comp;
    q->elements = malloc(sizeof(PQElement) * size);
    q->size = size;
    q->num = 1;

    return q;
}

void pq_push(PQueue q, const void *data) {
    PQElement *b;
    int n, m;

    if (q->num >= q->size) {
        q->size *= 2;
        b = q->elements = realloc(q->elements, sizeof(PQElement) * q->size);
    } else {
        b = q->elements;
    }

    n = q->num++;

    while ((m = n / 2) && q->comparator(data, b[m])) {
        b[n] = b[m];
        n = m;
    }

    b[n] = (PQElement *)data;
}

void* pq_pop(PQueue q) {
    void *out;

    if (q->num == 1) return 0;

    PQElement *b = q->elements;

    out = b[1];

    --q->num;

    int n = 1, m;
    while ((m = n * 2) < q->num) {
        if (m + 1 < q->num && !q->comparator(b[m], b[m + 1])) m++;
        if (q->comparator(b[q->num], b[m])) break;
        b[n] = b[m];
        n = m;
    }

    b[n] = b[q->num];
    if (q->num < q->size / 2 && q->num >= 16)
        q->elements = realloc(q->elements, (q->size /= 2) * sizeof(b[0]));
    return out;
}

int pq_empty(PQueue q) {
    return q->num <= 1;
}
