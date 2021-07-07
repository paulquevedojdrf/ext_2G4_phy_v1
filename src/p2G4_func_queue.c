/*
 * Copyright 2018 Oticon A/S
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "p2G4_func_queue.h"
#include "bs_oswrap.h"
#include "bs_tracing.h"
#include <stdlib.h>

/*
 * Array with one element per device interface
 * Each interface can have 1 function pending
 */
static fq_element_t *f_queue = NULL;
static uint32_t *device_list = NULL;

static uint32_t next_idx = 0;
static uint32_t n_devs = 0;
static bool flush_pend = false;
static bool force_sort = false;

static queable_f fptrs[N_funcs];

void fq_init(uint32_t n_dev){
  device_list = bs_calloc(n_dev, sizeof(uint32_t));
  f_queue = bs_calloc(n_dev, sizeof(fq_element_t));
  n_devs = n_dev;

  for (int i = 0 ; i < n_devs; i ++) {
    f_queue[i].time = TIME_NEVER;
    f_queue[i].f_index = None;
    device_list[i] = i;
  }
}

void fq_register_func(f_index_t type, queable_f fptr) {
  fptrs[type] = fptr;
}

/**
 * Find the next function which should be executed,
 * Based on the following order, from left to right:
 *  time (lower first), function index (higher first), device number (lower first)
 */
static int fq_compare(const void *pa, const void *pb)
{
    uint32_t a = *(uint32_t *)pa;
    uint32_t b = *(uint32_t *)pb;
    fq_element_t *el_a = &f_queue[a];
    fq_element_t *el_b = &f_queue[b];

    if (el_a->time < el_b->time) {
        return -1;
    }
    if (el_a->time > el_b->time) {
        return 1;
    }
    uint32_t prio_a = ((uint32_t)el_a->f_index << 24) | (uint32_t)(n_devs - a);
    uint32_t prio_b = ((uint32_t)el_b->f_index << 24) | (uint32_t)(n_devs - b);

    return ((prio_a > prio_b) ? -1 : 1);
}

static inline void fq_sort()
{
    qsort(device_list, n_devs, sizeof(*device_list), fq_compare);
    next_idx = 0;
}

static inline void fq_add_entry(bs_time_t time, f_index_t index, uint32_t dev_nbr)
{
  fq_element_t *el = &f_queue[dev_nbr];
  if (el->time == time) {
      force_sort = true;
  }
  el->time = time;
  el->f_index = index;
}

/**
 * Add a function for dev_nbr to the queue
 */
void fq_add(bs_time_t time, f_index_t index, uint32_t dev_nbr) {
  fq_add_entry(time, index, dev_nbr);
}

/**
 * Add a function for dev_nbr to the queue that should run at the next
 * operation.
 *
 * @param time  The deadline for this function. The function must run
 *              before or at this time limit
 */
void fq_add_pend(bs_time_t time, f_index_t index, uint32_t dev_nbr) {
  fq_add_entry(time, index, dev_nbr);
  f_queue[dev_nbr].pend = true;
  flush_pend = true;
}

/**
 * Remove an element from the queue and reorder it
 */
void fq_remove(uint32_t d){
  f_queue[d].f_index = None;
  f_queue[d].time = TIME_NEVER;
  f_queue[d].pend = false;
}

/**
 * The function queue has been seeded. Sort it and get ready for execution
 */
void fq_start(){
    fq_sort();
}

/**
 * Advance the function queue to the next element from the current time value
 */
void fq_step(bs_time_t current_time){
    if (force_sort) {
        force_sort = false;
    }
    else {
        next_idx++;
        if (current_time == f_queue[device_list[next_idx]].time) {
            return;
        }
    }
    force_sort = false;
    fq_sort();

    if (flush_pend) {
        flush_pend = false;

        uint32_t head_dev = device_list[0];

        bs_time_t now = f_queue[head_dev].time;
        for (int i = 0; i < n_devs; i++) {
            fq_element_t *el = &f_queue[i];
            if (el->pend) {
                el->pend = false;
                el->time = now;
            }
        }
        fq_sort();
    }
}

/**
 * Call the next function in the queue
 * Note: The function itself is left in the queue.
 */
void fq_call_next(){
  uint32_t dev_nbr = device_list[next_idx];
  f_queue[dev_nbr].pend = false;
  fptrs[f_queue[dev_nbr].f_index](dev_nbr);
}

/**
 * Get the time of the next element of the queue
 */
bs_time_t fq_get_next_time(){
  return f_queue[device_list[next_idx]].time;
}

void fq_free(){
  if ( f_queue != NULL ) {
    free(f_queue);
    free(device_list);
  }
}
