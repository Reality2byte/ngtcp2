/*
 * ngtcp2
 *
 * Copyright (c) 2017 ngtcp2 contributors
 * Copyright (c) 2012 nghttp2 contributors
 *
 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sublicense, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE
 * LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION
 * OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION
 * WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */
#include "ngtcp2_map.h"

#include <string.h>
#include <assert.h>
#include <stdio.h>

#include "ngtcp2_conv.h"

#define NGTCP2_INITIAL_HASHBITS 4

void ngtcp2_map_init(ngtcp2_map *map, uint64_t seed, const ngtcp2_mem *mem) {
  map->mem = mem;
  map->hashbits = 0;
  map->table = NULL;
  map->seed = seed;
  map->size = 0;
}

void ngtcp2_map_free(ngtcp2_map *map) {
  if (!map) {
    return;
  }

  ngtcp2_mem_free(map->mem, map->table);
}

int ngtcp2_map_each(const ngtcp2_map *map, int (*func)(void *data, void *ptr),
                    void *ptr) {
  int rv;
  size_t i;
  ngtcp2_map_bucket *bkt;
  size_t tablelen;

  if (map->size == 0) {
    return 0;
  }

  tablelen = 1u << map->hashbits;

  for (i = 0; i < tablelen; ++i) {
    bkt = &map->table[i];

    if (bkt->data == NULL) {
      continue;
    }

    rv = func(bkt->data, ptr);
    if (rv != 0) {
      return rv;
    }
  }

  return 0;
}

static size_t map_hash(const ngtcp2_map *map, ngtcp2_map_key_type key) {
  /* hasher from
     https://github.com/rust-lang/rustc-hash/blob/dc5c33f1283de2da64d8d7a06401d91aded03ad4/src/lib.rs
     We do not perform finalization here because we use top bits
     anyway. */
  key += map->seed;
  key *= 0xf1357aea2e62a9c5ull;
  return (size_t)((key * 11400714819323198485llu) >> (64 - map->hashbits));
}

static void map_bucket_swap(ngtcp2_map_bucket *a, ngtcp2_map_bucket *b) {
  ngtcp2_map_bucket c = *a;

  *a = *b;
  *b = c;
}

#ifndef WIN32
void ngtcp2_map_print_distance(const ngtcp2_map *map) {
  size_t i;
  size_t idx;
  ngtcp2_map_bucket *bkt;
  size_t tablelen;

  if (map->size == 0) {
    return;
  }

  tablelen = 1u << map->hashbits;

  for (i = 0; i < tablelen; ++i) {
    bkt = &map->table[i];

    if (bkt->data == NULL) {
      fprintf(stderr, "@%zu <EMPTY>\n", i);
      continue;
    }

    idx = map_hash(map, bkt->key);
    fprintf(stderr, "@%zu hash=%zu key=%" PRIu64 " base=%zu distance=%u\n", i,
            map_hash(map, bkt->key), bkt->key, idx, bkt->psl);
  }
}
#endif /* !defined(WIN32) */

static int map_insert(ngtcp2_map *map, ngtcp2_map_key_type key, void *data) {
  size_t idx = map_hash(map, key);
  ngtcp2_map_bucket b = {
    .key = key,
    .data = data,
  };
  ngtcp2_map_bucket *bkt;
  size_t mask = (1u << map->hashbits) - 1;

  for (;;) {
    bkt = &map->table[idx];

    if (bkt->data == NULL) {
      *bkt = b;
      ++map->size;
      return 0;
    }

    if (b.psl > bkt->psl) {
      map_bucket_swap(bkt, &b);
    } else if (bkt->key == key) {
      /* TODO This check is just a waste after first swap or if this
         function is called from map_resize.  That said, there is no
         difference with or without this conditional in performance
         wise. */
      return NGTCP2_ERR_INVALID_ARGUMENT;
    }

    ++b.psl;
    idx = (idx + 1) & mask;
  }
}

static int map_resize(ngtcp2_map *map, size_t new_hashbits) {
  size_t i;
  ngtcp2_map_bucket *bkt;
  size_t tablelen;
  int rv;
  ngtcp2_map new_map = {
    .table = ngtcp2_mem_calloc(map->mem, 1u << new_hashbits,
                               sizeof(ngtcp2_map_bucket)),
    .mem = map->mem,
    .seed = map->seed,
    .hashbits = new_hashbits,
  };
  (void)rv;

  if (new_map.table == NULL) {
    return NGTCP2_ERR_NOMEM;
  }

  if (map->size) {
    tablelen = 1u << map->hashbits;

    for (i = 0; i < tablelen; ++i) {
      bkt = &map->table[i];
      if (bkt->data == NULL) {
        continue;
      }

      rv = map_insert(&new_map, bkt->key, bkt->data);

      assert(0 == rv);
    }
  }

  ngtcp2_mem_free(map->mem, map->table);
  map->table = new_map.table;
  map->hashbits = new_hashbits;

  return 0;
}

int ngtcp2_map_insert(ngtcp2_map *map, ngtcp2_map_key_type key, void *data) {
  int rv;

  assert(data);

  /* Load factor is 7/8 */
  /* Under the very initial condition, that is map->size == 0 and
     map->hashbits == 0, 8 > 7 still holds nicely. */
  if ((map->size + 1) * 8 > (1u << map->hashbits) * 7) {
    if (map->hashbits) {
      rv = map_resize(map, map->hashbits + 1);
      if (rv != 0) {
        return rv;
      }
    } else {
      rv = map_resize(map, NGTCP2_INITIAL_HASHBITS);
      if (rv != 0) {
        return rv;
      }
    }
  }

  rv = map_insert(map, key, data);
  if (rv != 0) {
    return rv;
  }

  return 0;
}

void *ngtcp2_map_find(const ngtcp2_map *map, ngtcp2_map_key_type key) {
  size_t idx;
  ngtcp2_map_bucket *bkt;
  size_t psl = 0;
  size_t mask;

  if (map->size == 0) {
    return NULL;
  }

  idx = map_hash(map, key);
  mask = (1u << map->hashbits) - 1;

  for (;;) {
    bkt = &map->table[idx];

    if (bkt->data == NULL || psl > bkt->psl) {
      return NULL;
    }

    if (bkt->key == key) {
      return bkt->data;
    }

    ++psl;
    idx = (idx + 1) & mask;
  }
}

int ngtcp2_map_remove(ngtcp2_map *map, ngtcp2_map_key_type key) {
  size_t idx;
  ngtcp2_map_bucket *b, *bkt;
  size_t psl = 0;
  size_t mask;

  if (map->size == 0) {
    return NGTCP2_ERR_INVALID_ARGUMENT;
  }

  idx = map_hash(map, key);
  mask = (1u << map->hashbits) - 1;

  for (;;) {
    bkt = &map->table[idx];

    if (bkt->data == NULL || psl > bkt->psl) {
      return NGTCP2_ERR_INVALID_ARGUMENT;
    }

    if (bkt->key == key) {
      b = bkt;
      idx = (idx + 1) & mask;

      for (;;) {
        bkt = &map->table[idx];
        if (bkt->data == NULL || bkt->psl == 0) {
          b->data = NULL;
          break;
        }

        --bkt->psl;
        *b = *bkt;
        b = bkt;

        idx = (idx + 1) & mask;
      }

      --map->size;

      return 0;
    }

    ++psl;
    idx = (idx + 1) & mask;
  }
}

void ngtcp2_map_clear(ngtcp2_map *map) {
  if (map->size == 0) {
    return;
  }

  memset(map->table, 0, sizeof(*map->table) * (1u << map->hashbits));
  map->size = 0;
}

size_t ngtcp2_map_size(const ngtcp2_map *map) { return map->size; }
