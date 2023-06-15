#include "queue.h"
#include <threads.h>
#include <stdatomic.h>

struct value_queue_node {
  void *value;
  struct value_queue_node *next;
};

struct value_queue {
  struct value_queue_node *head;
  struct value_queue_node *tail;
};
atomic_size_t value_size;

struct waiting_queue_node {
  cnd_t *cond;
  struct waiting_queue_node *next;
};

struct waiting_queue {
    struct waiting_queue_node *head;
    struct waiting_queue_node *tail;
};
atomic_size_t waiting;

mtx_t value_enqueue_lock;
mtx_t value_head_lock;
atomic_size_t visited;

struct value_queue value_queue; //initialized once and used exclusively so no * needed
struct waiting_queue waiting_queue;


void initQueue(void) {
    value_queue.head = NULL;
    value_queue.tail = NULL;

    waiting_queue.head = NULL;
    waiting_queue.tail = NULL;

    mtx_init(&value_enqueue_lock, mtx_plain);

    mtx_init(&value_head_lock, mtx_plain);

    value_size = 0;
    waiting = 0;
    visited = 0;

    return;
}

void destroyQueue(void) {
    struct value_queue_node *node;
    struct waiting_queue_node *w_node, *w_next;

    mtx_destroy(&value_enqueue_lock);

    mtx_destroy(&value_head_lock);

    for (node = value_queue.head; node != NULL; node = node->next) {
        free(node);
    }
    for (w_node = waiting_queue.head; w_node != NULL; w_node = w_next) {
        // this loop shouldn't happen.
        // we free the nodes JIC.
        cnd_destroy(w_node->cond);
        w_next = w_node->next;
        free(w_node);
    }

    return;
}

void add_to_value_queue(struct value_queue_node *node) {

    lock(&value_enqueue_lock);

        if (value_queue.tail != NULL) { // common case

            value_queue.tail->next = node;
            value_queue.tail = node;

        } else { // value queue is empty. need to take care of a few conditions.
            lock(&value_head_lock); // make sure we have control of the first argument

                // insert the node
                value_queue.head = node;
                value_queue.tail = node;

                if (waiting_queue.head != NULL) { // if there are waiting threads, alert the head
                    remove_from_waiting_queue();
                }
                // tricky race might occur when at the same time, a thread is inserted to the waiting queue.
                // this condition cannot happen in our current implementation.
                // a value is inserted to waiting queue only in remove_from_value_queue, which is locked by value_head_lock.

            unlock(&value_head_lock);

        }
    unlock(&value_enqueue_lock);
    value_size++;

    return;
}

void* remove_from_value_queue(void) {
    struct value_queue_node *node;
    void *value;
    cnd_t *cond;

    value_size--;
    lock(&value_head_lock);

        node = value_queue.head;
        if (node == NULL) {
            cnd_init(cond);
            add_to_waiting_queue(cond);
            cnd_wait(cond, &value_head_lock);
            node = value_queue.head;
        }
        // if there is a tryDequeue after cnd_signal is called on the thread, but before it executes,
        // we need to make sure we have the node
        while (node == NULL) {
            add_to_waiting_queue_head(cond);
            cnd_wait(cond, &value_head_lock);
            node = value_queue.head;
        }


        value = node->value;
        value_queue.head = value_queue.head->next;
        if (value_queue.head == NULL)
            value_queue.tail = NULL;

        // waiting doesn't have to be locked because add_to/remove_from_waiting_queue are locked by value_head_lock in all cases.
        if (value_queue.head != NULL && waiting_queue.head != NULL) {
            remove_from_waiting_queue();
        }
    
    unlock(&value_head_lock);

    visited++;

    cnd_destroy(cond);
    free(node);
    return value;
}

bool try_remove_from_value_queue(void** value_p) {
    void* value
    lock(&value_head_lock);

        if (value_queue.head != NULL) {
            *value_p = value_queue.head->value;
            return true;
        }
        return false;

    unlock(&value_head_lock);
}

// this function is protected by value_head_lock exclusively, so it doesn't need locks
void add_to_waiting_queue(cnd_t *cond) {
    struct waiting_queue_node *node;

    node = (struct waiting_queue_node*)malloc(sizeof(struct waiting_queue_node));
    node->cond = cond;
    node->next = NULL;

    if (waiting_queue.tail == NULL) {
        waiting_queue.head = node;
        waiting_queue.tail = node;

    } else {
        waiting_queue.tail->next = node;
        waiting_queue.tail = node;
    }

    waiting++;

    return;
}

void add_to_waiting_queue_head(cnd_t *cond) {
    struct waiting_queue_node *node;

    node = (struct waiting_queue_node*)malloc(sizeof(struct waiting_queue_node));
    node->cond = cond;
    node->next = waiting_queue.head;

    waiting_queue.head = node;
    if (waiting_queue.tail == NULL)
        waiting_queue.tail = node;

    waiting++;
}

// this function is protected by value_head_lock exclusively, so it doesn't need locks
void remove_from_waiting_queue() {
    struct waiting_queue_node *node;
    cnd_t *cond;

    node = waiting_queue.head;
    if (node == NULL)
        return;

    waiting_queue.head = waiting_queue.head->next;

    cond = node->cond;
    waiting--;
    free(node);

    cnd_signal(cond);
    return;
}

size_t size(void) {
    return value_size;
}

size_t waiting(void) {
    return waiting;
}

size_t visited(void) {
    return visited;
}

void enqueue(const void *value) {
    struct value_queue_node *node;

    node = (struct value_queue_node*)malloc(sizeof(struct value_queue_node));
    node->value = value;
    node->next = NULL;

    add_to_value_queue(node);
}

void* dequeue(void) {
    return remove_from_value_queue();
}

bool tryDequeue(void** value_p) {
    return try_remove_from_value_queue(value_p);
}