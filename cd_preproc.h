#ifndef CD_PREPROC_H
#define CD_PREPROC_H


#ifdef __cplusplus
extern "C" {
#endif
#include <stddef.h>
#include <stdint.h>

typedef enum {
    CD_PP_LOGLEVEL_DEBUG,
    CD_PP_LOGLEVEL_WARNING,
    CD_PP_LOGLEVEL_ERROR
} cd_pp_loglevel_t;

typedef void (*cd_pp_log_func_t)(void* log_data, cd_pp_loglevel_t level, const char* msg);

typedef struct {
    size_t*             keys;
    size_t*             vals;
    size_t              fill;
    size_t              capacity;
} cd_pp_map_t;

typedef struct {
    uint8_t*            first;
    uint8_t*            curr;
    size_t              fill;
    size_t              capacity;
} cd_pp_arena_t;

typedef struct cd_pp_str_item_t cd_pp_str_item_t;
struct cd_pp_str_item_t {
    cd_pp_str_item_t*   next;
    size_t              length;
    char                str[0];
};

typedef struct {
    cd_pp_log_func_t    log_func;
    void*               log_data;
    cd_pp_map_t         str_map;
    cd_pp_arena_t       str_mem;
} cd_pp_state_t;

const char* cd_pp_str_intern(cd_pp_state_t* state, const char* begin, const char* end);

void cd_pp_state_free(cd_pp_state_t* state);

#ifdef CD_PREPROC_IMPLEMENTATION
#include <assert.h>
#include <stdbool.h>
#include <string.h>
#include <stdlib.h>

void* cd_pp_malloc(size_t size)
{
    void* rv = malloc(size);
    assert(rv != NULL);
    return rv;
}

void* cd_pp_calloc(size_t num, size_t size)
{
    void* rv = calloc(num, size);
    assert(rv != NULL);
    return rv;
}

void* cd_pp_arena_alloc(cd_pp_state_t* state, cd_pp_arena_t* arena, size_t bytes)
{
    if(bytes == 0) return NULL;
    
    size_t padded = (bytes + sizeof(void*) - 1) & ~(sizeof(void*));
    if(arena->capacity < arena->fill + padded) {
        const size_t page_size = 1024;
        arena->fill = sizeof(uint8_t*);
        arena->capacity = (arena->fill + padded) < page_size ? page_size : (arena->fill + padded);
        
        uint8_t* page = cd_pp_malloc(arena->capacity);
        *((uint8_t**)page) = NULL;
        if(arena->first == NULL) {
            arena->first = page;
            arena->curr = page;
        }
        else {
            *(uint8_t**)arena->curr = page;
            arena->curr = page;
        }
    }
    
    assert(arena->first);
    assert(arena->curr);
    assert(arena->fill + padded <= arena->capacity);
    
    void* rv = arena->curr + arena->fill;
    arena->fill += padded;
    return rv;
}

void cd_pp_arena_free(cd_pp_arena_t* arena)
{
    uint8_t* page = arena->first;
    while(page != NULL) {
        uint8_t* next = *(uint8_t**)page;
        free(page);
        page = next;
    }
    *arena = (cd_pp_arena_t){0};
}

size_t cd_pp_hash_size_t(size_t value)
{
    value *= 0xff51afd7ed558ccd;
    value ^= value >> 32;
    return value;
}

size_t cd_pp_map_get(const cd_pp_map_t* map, size_t key)
{
    assert(key != 0);
    if(map->fill == 0) return 0;
    
    size_t mask = map->capacity-1;
    for(size_t i=cd_pp_hash_size_t(key); true; i++) {
        i = i & mask;
        if(map->keys[i] == key) {
            return map->vals[i];
        }
        else if(map->keys[i] == 0) {
            return 0;
        }
    }
}

void cd_pp_map_insert(cd_pp_map_t* map, size_t key, size_t val)
{
    assert(key != 0);                       // 0 is used to tag empty
    assert(val != 0);                       // zero val is used for not found
    if(map->capacity <= 2 * map->fill) {    // Resize and rehash
        size_t old_capacity = map->capacity;
        size_t* old_keys = map->keys;
        size_t* old_vals = map->vals;
        map->fill = 0;
        map->capacity = map->capacity ? 2 * map->capacity : 16;
        assert((map->capacity & (map->capacity-1)) == 0);
        map->keys = (size_t*)cd_pp_calloc(map->capacity, sizeof(size_t));
        map->vals = (size_t*)cd_pp_malloc(map->capacity * sizeof(size_t));
        for(size_t i=0; i<old_capacity; i++) {
            if(old_keys[i]) {
                cd_pp_map_insert(map, old_keys[i], old_vals[i]);
            }
        }
    }
    size_t mask = map->capacity - 1;
    for(size_t i=cd_pp_hash_size_t(key); true; i++) {
        i = i & mask;
        if(map->keys[i] == key) {
            map->vals[i] = val;
            break;
        }
        else if(map->keys[i] == 0) {
            map->keys[i] = key;
            map->vals[i] = val;
            map->fill++;
            break;
        }
    }
}

void cd_pp_map_free(cd_pp_map_t* map)
{
    free(map->keys);
    free(map->vals);
    *map = (cd_pp_map_t){0};
}

size_t cd_pp_fnv_1a(const char* begin, const char* end)
{
    assert(begin <= end);
    if(sizeof(size_t) == sizeof(uint32_t)) {
        uint32_t hash = 0x811c9dc5;
        for (const char * p = begin; p < end; p++) {
            hash = hash ^ *p;
            hash = hash * 0x01000193;
        }
        return (size_t)hash;
    }
    else {
        uint64_t hash = 0xcbf29ce484222325;
        for (const char * p = begin; p < end; p++) {
            hash = hash ^ *p;
            hash = hash * 0x100000001B3;
        }
        return (size_t)hash;
    }
}

const char* cd_pp_str_intern(cd_pp_state_t* state, const char* begin, const char* end)
{
    assert(state);
    assert(begin <= end);
    if(begin == NULL || begin == end) {
        return NULL;
    }
    size_t length = end - begin;

    uint64_t hash = cd_pp_fnv_1a(begin, end);
    hash = hash ? hash : 1; // hash should never be zero
    
    cd_pp_str_item_t* item = (cd_pp_str_item_t*)cd_pp_map_get(&state->str_map, hash);
    for(cd_pp_str_item_t* i = item; i != NULL; i = i->next) {
        if(i->length == length) {
            if(strncmp(i->str, begin, length)==0) {
                return i->str;
            }
        }
    }
    
    cd_pp_str_item_t* new_item = (cd_pp_str_item_t*)cd_pp_arena_alloc(state, &state->str_mem, sizeof(cd_pp_str_item_t) + length + 1);
    new_item->next = item;
    new_item->length = length;
    memcpy(new_item->str, begin, length);
    new_item->str[length] = '\0';
    cd_pp_map_insert(&state->str_map, hash, (size_t)new_item);
    return new_item->str;
}

void cd_pp_state_free(cd_pp_state_t* state)
{
    cd_pp_map_free(&state->str_map);
    cd_pp_arena_free(&state->str_mem);
}



#endif // of CD_PREPROC_IMPLEMENTATION

#ifdef __cplusplus
}   // extern "C"
#endif

#endif // of CD_PREPROC_H
