/*
 * Copyright 2018 Oticon A/S
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "p2G4_func_queue.h"
#include "bs_oswrap.h"
#include "bs_tracing.h"

/*
 * Array with one element per device interface
 * Each interface can have 1 function pending
 */
static fq_element_t *f_queue = NULL;

static uint32_t next_d = 0;
static uint32_t n_devs = 0;

static queable_f fptrs[N_funcs];

void fq_init(uint32_t n_dev){
  f_queue = bs_calloc(n_dev, sizeof(fq_element_t));
  n_devs = n_dev;

  for (int i = 0 ; i < n_devs; i ++) {
    f_queue[i].time = TIME_NEVER;
    f_queue[i].f_index = None;
  }
}

void fq_register_func(f_index_t type, queable_f fptr) {
  fptrs[type] = fptr;
}

/**
 * Find the next function which should be executed,
 * Based on the following order, from left to right:
 *  time (lower first), function index (higher first), device number (lower first)
 * TOOPT: The whole function queue is implemented in a simple/naive way,
 *        which is perfectly for simulations with a few devices.
 *        But, if there is many devices, this would be quite slow.
 */
static inline void fq_find_next(){
  bs_time_t chosen_f_time;
  next_d = 0;
  chosen_f_time = f_queue[0].time;

  for (int i = 1; i < n_devs; i ++) {
    fq_element_t *el = &f_queue[i];
    if (el->time < chosen_f_time) {
      next_d = i;
      chosen_f_time = el->time;
      continue;
    } else if (el->time == chosen_f_time) {
      if (el->f_index > f_queue[next_d].f_index) {
        next_d = i;
        chosen_f_time = el->time;
        continue;
      }
    }
  }
}

/**
 * Add a function for dev_nbr to the queue and reorder it
 */
void fq_add(bs_time_t time, f_index_t index, uint32_t dev_nbr) {
  fq_element_t *el = &f_queue[dev_nbr];
  el->time = time;
  el->f_index = index;
  el->pend = false;
  //printf("fq_add dev %u until %llu f_index %d\n", dev_nbr, time, index);
  //fq_find_next();
}

static bs_time_t pend_time = 0;
void fq_pend_until(bs_time_t time, f_index_t index, uint32_t dev_nbr) {
  fq_element_t *el = &f_queue[dev_nbr];
  pend_time = el->time;
  el->time = time;
  el->f_index = index;
  el->pend = true;
  //fq_find_next();
  //printf("fq_pend_until dev %u until %llu\n", dev_nbr, time);
}

/**
 * Remove an element from the queue and reorder it
 */
void fq_remove(uint32_t d){
  f_queue[d].f_index = None;
  f_queue[d].time = TIME_NEVER;

  //fq_find_next();
}

void fq_step() {
    fq_find_next();
    bs_time_t now = fq_get_next_time();
    if (pend_time && now != pend_time) {
        pend_time = 0;
        for (int i = 0; i < n_devs; i++) {
            fq_element_t *el = &f_queue[i];
            if (!el->pend) {
                continue;
            }
            el->pend = false;
            el->time = now;
            if (el->f_index > f_queue[next_d].f_index) {
                next_d = i;
                //printf("   next_d is now %u f_index %d\n", next_d, f_queue[next_d].f_index);
            }
        }
    }
    //printf("fq_step resolved next_d %u time %llu f_index %d\n",
    //        next_d, f_queue[next_d].time, f_queue[next_d].f_index);
}

/**
 * Call the next function in the queue
 * Note: The function itself is left in the queue.
 */
void fq_call_next(){
  f_queue[next_d].pend = false;
  fptrs[f_queue[next_d].f_index](next_d);
}

/**
 * Get the time of the next element of the queue
 */
bs_time_t fq_get_next_time(){
  return f_queue[next_d].time;
}

void fq_free(){
  if ( f_queue != NULL )
    free(f_queue);
}
