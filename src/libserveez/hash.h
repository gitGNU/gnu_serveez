/*
 * hash.h - hash function interface
 *
 * Copyright (C) 2000, 2001 Stefan Jahn <stefan@lkcc.org>
 *
 * This is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2, or (at your option)
 * any later version.
 * 
 * This software is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with this package; see the file COPYING.  If not, write to
 * the Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 *
 * $Id: hash.h,v 1.3 2001/03/08 11:53:56 ela Exp $
 *
 */

#ifndef __HASH_H__
#define __HASH_H__ 1

#include "libserveez/defines.h"

/* useful defines */
#define HASH_SHRINK 4
#define HASH_EXPAND 8
#define HASH_MIN_SIZE 4

/*
 * This is the basic structure of a hash entry consisting of its
 * key, the actual value stored in the hash table and the hash code
 * of the key.
 */
typedef struct
{
  unsigned long code;
  char *key;
  void *value;
}
svz_hash_entry_t;

/*
 * The hash table consists of different hash buckets. This contains the
 * bucket's size and the entry array.
 */
typedef struct
{
  int size;
  svz_hash_entry_t *entry;
}
svz_hash_bucket_t;

/*
 * This structure keeps information of a specific hash table.
 */
typedef struct
{
  int buckets;                     /* number of buckets in the table */
  int fill;                        /* number of filled buckets */
  int keys;                        /* number of stored keys */
  int (* equals) (char *, char *); /* key string equality callback */
  unsigned long (* code) (char *); /* hash code calculation callback */
  unsigned (* keylen) (char *);    /* how to get the hash key length */
  svz_hash_bucket_t *table;        /* hash table */
}
svz_hash_t;

__BEGIN_DECLS

/*
 * Basic hash table functions.
 */
SERVEEZ_API svz_hash_t *svz_hash_create __P ((int size));
SERVEEZ_API void svz_hash_destroy __P ((svz_hash_t *hash));
SERVEEZ_API void svz_hash_clear __P ((svz_hash_t *hash));
SERVEEZ_API void *svz_hash_delete __P ((svz_hash_t *hash, char *key));
SERVEEZ_API void svz_hash_put __P ((svz_hash_t *hash, char *key, void *value));
SERVEEZ_API void *svz_hash_get __P ((svz_hash_t *hash, char *key));
SERVEEZ_API void **svz_hash_values __P ((svz_hash_t *hash));
SERVEEZ_API char **svz_hash_keys __P ((svz_hash_t *hash));
SERVEEZ_API int svz_hash_size __P ((svz_hash_t *hash));
SERVEEZ_API int svz_hash_capacity __P ((svz_hash_t *hash));
SERVEEZ_API char *svz_hash_contains __P ((svz_hash_t *hash, void *value));

__END_DECLS

#if DEBUG_MEMORY_LEAKS
# include <stdlib.h>
# define svz_hash_xfree(ptr) svz_free_func (ptr)
#else
# define svz_hash_xfree(ptr) svz_free (ptr);
#endif

#endif /* not __HASH_H__ */