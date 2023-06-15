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
size_t value_size;

struct waiting_queue_node {
  cnd_t *cond;
  bool woken_up; //value will only be changed once from false to true so no concurrency issues
  struct waiting_queue_node *next;
};

struct waiting_queue {
    struct waiting_queue_node *head;
    struct waiting_queue_node *tail;
};
atomic_size_t waiting;

mtx_t value_enqueue_lock, waiting_enqueue_lock;
mtx_t value_head_lock, waiting_head_lock;
atomic_size_t visited;

struct value_queue *value_queue;
struct waiting_queue *waiting_queue;


void initQueue(void) {
    value_queue = (struct value_queue)malloc(sizeof(struct value_queue));
    value_queue->head = NULL;
    value_queue->tail = NULL;

    waiting_queue = (struct waiting_queue)malloc(sizeof(struct waiting_queue));
    waiting_queue->head = NULL;
    waiting_queue->tail = NULL;

    mtx_init(&value_enqueue_lock, mtx_plain);
    mtx_init(&waiting_enqueue_lock, mtx_plain);

    mtx_init(&value_head_lock, mtx_plain);
    mtx_init(&waiting_head_lock, mtx_plain);

    value_size = 0;
    waiting = 0;
    visited = 0;

    return;
}

void destroyQueueu(void) {
    struct value_queue_node *node;
    struct waiting_queue_node *w_node;

    mtx_destroy(&value_enqueue_lock);
    mtx_destroy(&waiting_enqueue_lock);

    mtx_destroy(&value_head_lock);
    mtx_destroy(&waiting_head_lock);

    for (node = value_queue->head; node != NULL; node = node->next) {
        free(node);
    }
    for (w_node = waiting_queue->head; w_node != NULL; w_node = w_node->next) {
        // this loop shouldn't happen.
        // we free the nodes JIC.
        free(w_node);
    }

    free(value_queue);
    free(waiting_queue);

    return;
}

void add_to_value_queue(struct value_queue_node *node) {

    lock(&value_enqueue_lock);

        if (value_queue->tail != NULL) { // common case

            value_queue->tail->next = node;
            value_queue->tail = node;

        } else { // value queue is empty. need to take care of a few conditions.
            lock(&value_head_lock); // make sure we have control of the first argument

                // insert the node
                value_queue->head = node;
                value_queue->tail = node;

                if (waiting > 0) { // if there are waiting threads, alert the head
                    remove_from_waiting_queue(waiting_queue);
                }
                // tricky race might occur when at the same time, a thread is inserted to the waiting queue.
                // this condition cannot happen in our current implementation.
                // a value is inserted to waiting queue only in remove_from_value_queue, which is locked by value_head_lock.

            unlock(&value_head_lock);

        }
    value_size++;
    unlock(&value_enqueue_lock);

    return;
}

void* remove_from_value_queue(void) {
    struct value_queue_node *node;
    void *value;
    cnd_t *cond;


    lock(&value_head_lock);
        value_size--;

        node = value_queue->head;
        if (node == NULL) {

            cnd_init(cond);
            add_to_waiting_queue(waiting_queue, cond);
            cnd_wait(cond, &value_head_lock);
            node = value_queue->head;
        }

        visited++;
        value = node->value;
        value_queue->head = value_queue->head->next;

        // waiting doesn't have to be locked because add_to/remove_from_waiting_queue are locked by value_head_lock in all cases.
        if (value_size > 0 && waiting > 0) {
            remove_from_waiting_queue(waiting_queue);
        }
    
    unlock(&value_head_lock);

    free(node);
    return value;
}

void add_to_waiting_queue(struct waiting_queue *queue, cnd_t *cond) {
    struct waiting_queue_node *node;


    node = (struct waiting_queue_node)malloc(sizeof(struct waiting_queue_node));
    node->cond = cond;
    node->next = NULL;
    node->woken_up = false;

    lock(&waiting_enqueue_lock);

        if (queue->tail == NULL) {
            lock(&waiting_head_lock);

                queue->head = node;
                queue->tail = node;
            
            unlock(&waiting_head_lock);

        } else {
            queue->tail->next = node;
            queue->tail = node;
        }
    
    unlock(&waiting_enqueue_lock);

    waiting++;

    return;
}

void remove_from_waiting_queue(struct waiting_queue *queue) {
    struct waiting_queue_node *node;

    lock(&waiting_head_lock);

        node = queue->head;
        if (node == NULL)
            return;

        queue->head = queue->head->next;

        cnd_signal(node->cond);
    
    unlock(&waiting_head_lock);

    waiting--;
    free(node);

    return NULL;
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

    node = (struct value_queue_node)malloc(sizeof(struct value_queue_node));
    node->value = value;
    node->next = NULL;

    add_to_value_queue(node);
}

void* dequeue(void) {
    return remove_from_value_queue();
}