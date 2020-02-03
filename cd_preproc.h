#ifndef CD_PREPROC_H
#define CD_PREPROC_H


#ifdef __cplusplus
extern "C" {
#endif
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

typedef enum {
    CD_PP_LOGLEVEL_DEBUG,
    CD_PP_LOGLEVEL_WARN,
    CD_PP_LOGLEVEL_ERROR
} cd_pp_loglevel_t;

typedef void (*cd_pp_log_func_t)(void* log_data, cd_pp_loglevel_t level, const char* msg, ...);

typedef struct {
    const char* begin;
    const char* end;
} cd_pp_strview_t;

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

typedef bool (*cd_pp_handle_include_t)(void* handler_data, cd_pp_state_t* state, cd_pp_strview_t path);

const char* cd_pp_str_intern(cd_pp_state_t* state, cd_pp_strview_t str);

bool cd_pp_process(cd_pp_state_t*           state,
                   cd_pp_strview_t          input,
                   cd_pp_handle_include_t   handle_include,
                   void*                    handle_data);

void cd_pp_state_free(cd_pp_state_t* state);

#ifdef CD_PREPROC_IMPLEMENTATION
#include <assert.h>
#include <string.h>
#include <stdlib.h>

#define CD_PP_LOG_WRAP(state, level, ...) do {                  \
    if(state->log_func) {                                       \
        state->log_func(state->log_data, level, __VA_ARGS__);   \
    }                                                           \
} while(0)
#define CD_PP_LOG_DEBUG(state, ...) CD_PP_LOG_WRAP(state, CD_PP_LOGLEVEL_DEBUG, __VA_ARGS__)
#define CD_PP_LOG_WARN(state, ...) CD_PP_LOG_WRAP(state, CD_PP_LOGLEVEL_WARN, __VA_ARGS__)
#define CD_PP_LOG_ERROR(state, ...) CD_PP_LOG_WRAP(state, CD_PP_LOGLEVEL_ERROR, __VA_ARGS__)


typedef enum {
    CD_PP_TOKEN_EOF         = 0,
    CD_PP_TOKEN_LASTCHAR    = 255,
    CD_PP_TOKEN_DIGITS,
    CD_PP_TOKEN_IDENTIFIER,
    CD_PP_TOKEN_AND,
    CD_PP_TOKEN_OR,
    CD_PP_TOKEN_LESS_EQUAL,
    CD_PP_TOKEN_GREATER_EQUAL,
    CD_PP_TOKEN_EQUAL,
    CD_PP_TOKEN_NOT_EQUAL,
    CD_PP_TOKEN_SHIFT_LEFT,
    CD_PP_TOKEN_SHIFT_RIGHT,
    CD_PP_TOKEN_CONCAT,
    CD_PP_TOKEN_NEWLINE
} cd_pp_token_kind_t;

typedef struct {
    cd_pp_strview_t     text;
    cd_pp_token_kind_t  kind;
} cd_pp_token_t;

typedef struct {
    cd_pp_state_t*      state;
    cd_pp_strview_t     input;
    cd_pp_token_t       current;
    cd_pp_token_t       matched;
} cd_pp_ctx_t;

static bool cd_pp_isspace(char c)
{
    switch(c) {
    case ' ': case '\t': case '\v': case '\f': case '\r':
        return true;
        break;
    default:
        return false;
    }
}

static bool cd_pp_next_token(cd_pp_ctx_t* ctx)
{
    ctx->matched = ctx->current;
start:
    if(ctx->input.end <= ctx->input.begin) {
        ctx->current.text = (cd_pp_strview_t){0,0};
        ctx->current.kind = CD_PP_TOKEN_EOF;
        return true;
    }
    ctx->current.text.begin = ctx->input.begin;
    switch(*ctx->input.begin++) {
    case ' ': case '\t': case '\v': case '\f': case '\r':
        while (ctx->input.begin < ctx->input.end && (*ctx->input.begin == ' '  ||
                                                     *ctx->input.begin == '\t' ||
                                                     *ctx->input.begin == '\v' ||
                                                     *ctx->input.begin == '\f' ||
                                                     *ctx->input.begin == '\r'))
        {
            ctx->input.begin++;
        }
        goto start;
    case '0': case '1': case '2': case '3': case '4': case '5': case '6': case '7': case '8': case '9':
        ctx->current.kind = CD_PP_TOKEN_DIGITS;
        while(ctx->input.begin < ctx->input.end &&
              '0' <= *ctx->input.begin && *ctx->input.begin <= '9')
        {
            ctx->input.begin++;
        }
        break;
    case '_': case 'a': case 'b': case 'c': case 'd': case 'e':
    case 'f': case 'g': case 'h': case 'i': case 'j': case 'k':
    case 'l': case 'm': case 'n': case 'o': case 'p': case 'q':
    case 'r': case 's': case 't': case 'u': case 'v': case 'w':
    case 'x': case 'y': case 'z': case 'A': case 'B': case 'C':
    case 'D': case 'E': case 'F': case 'G': case 'H': case 'I':
    case 'J': case 'K': case 'L': case 'M': case 'N': case 'O':
    case 'P': case 'Q': case 'R': case 'S': case 'T': case 'U':
    case 'V': case 'W': case 'X': case 'Y': case 'Z':
        ctx->current.kind = CD_PP_TOKEN_IDENTIFIER;
        while(ctx->input.begin < ctx->input.end &&
              (*ctx->input.begin == '_' ||
               ('0' <= *ctx->input.begin && *ctx->input.begin <= '9') ||
               ('a' <= *ctx->input.begin && *ctx->input.begin <= 'Z') ||
               ('A' <= *ctx->input.begin && *ctx->input.begin <= 'Z')))
        {
            ctx->input.begin++;
        }
        break;
    case '/':
        ctx->current.kind = (cd_pp_token_kind_t)ctx->input.begin[-1];
        if(ctx->input.begin < ctx->input.end && *ctx->input.begin == '/') {
            // single-line comment
            do {
                ctx->input.begin++;
            }
            while(ctx->input.begin < ctx->input.end && *ctx->input.begin != '\n');
            goto start;
        }
        else if(ctx->input.begin < ctx->input.end && *ctx->input.begin == '*') {
            // multi-line comment
            ctx->input.begin++;
            while(ctx->input.begin + 1 < ctx->input.end) {
                if(ctx->input.begin[0] == '*' && ctx->input.begin[1] == '/') {
                    ctx->input.begin += 2;
                    goto start;
                }
                ctx->input.begin++;
            }
            CD_PP_LOG_ERROR(ctx->state, "EOF encountered while scanning for en of /* */");
            return false;
        }
        break;
    case '"':
        while(ctx->input.begin < ctx->input.end) {
            if(*ctx->input.begin++ == '"') goto done;
        }
        CD_PP_LOG_ERROR(ctx->state, "EOF encountered while scanning for end of \"...\"");
        return false;
        break;
    case '\n':
        ctx->current.kind = CD_PP_TOKEN_NEWLINE;
        break;
    case '&':
        ctx->current.kind = (cd_pp_token_kind_t)ctx->input.begin[-1];
        if(ctx->input.begin < ctx->input.end && *ctx->input.begin == '&') {
            ctx->current.kind = CD_PP_TOKEN_AND;
            ctx->input.begin++;
        }
        break;
    case '|':
        ctx->current.kind = (cd_pp_token_kind_t)ctx->input.begin[-1];
        if(ctx->input.begin < ctx->input.end && *ctx->input.begin == '|') {
            ctx->current.kind = CD_PP_TOKEN_OR;
            ctx->input.begin++;
        }
        break;
    case '<':
        ctx->current.kind = (cd_pp_token_kind_t)ctx->input.begin[-1];
        if(ctx->input.begin < ctx->input.end && *ctx->input.begin == '<') {
            ctx->current.kind = CD_PP_TOKEN_SHIFT_LEFT;
            ctx->input.begin++;
        }
        else if(ctx->input.begin < ctx->input.end && *ctx->input.begin == '=') {
            ctx->current.kind = CD_PP_TOKEN_LESS_EQUAL;
            ctx->input.begin++;
        }
        break;
    case '>':
        ctx->current.kind = (cd_pp_token_kind_t)ctx->input.begin[-1];
        if(ctx->input.begin < ctx->input.end && *ctx->input.begin == '>') {
            ctx->current.kind = CD_PP_TOKEN_SHIFT_RIGHT;
            ctx->input.begin++;
        }
        else if(ctx->input.begin < ctx->input.end && *ctx->input.begin == '=') {
            ctx->current.kind = CD_PP_TOKEN_GREATER_EQUAL;
            ctx->input.begin++;
        }
        break;
    case '=':
        ctx->current.kind = (cd_pp_token_kind_t)ctx->input.begin[-1];
        if(ctx->input.begin < ctx->input.end && *ctx->input.begin == '=') {
            ctx->current.kind = CD_PP_TOKEN_EQUAL;
            ctx->input.begin++;
        }
        break;
    case '!':
        ctx->current.kind = (cd_pp_token_kind_t)ctx->input.begin[-1];
        if(ctx->input.begin < ctx->input.end && *ctx->input.begin == '=') {
            ctx->current.kind = CD_PP_TOKEN_NOT_EQUAL;
            ctx->input.begin++;
        }
        break;
    case '#':
        ctx->current.kind = (cd_pp_token_kind_t)ctx->input.begin[-1];
        if(ctx->input.begin < ctx->input.end && *ctx->input.begin == '#') {
            ctx->current.kind = CD_PP_TOKEN_CONCAT;
            ctx->input.begin++;
        }
        break;

            
            
    default:
        ctx->current.kind = (cd_pp_token_kind_t)ctx->input.begin[-1];
        break;
    }
done:
    ctx->current.text.end = ctx->input.begin;
    return true;
}

static void* cd_pp_malloc(size_t size)
{
    void* rv = malloc(size);
    assert(rv != NULL);
    return rv;
}

static void* cd_pp_calloc(size_t num, size_t size)
{
    void* rv = calloc(num, size);
    assert(rv != NULL);
    return rv;
}

static void* cd_pp_arena_alloc(cd_pp_state_t* state, cd_pp_arena_t* arena, size_t bytes)
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
        CD_PP_LOG_DEBUG(state, "Allocated page of size %zu", arena->capacity);
    }
    
    assert(arena->first);
    assert(arena->curr);
    assert(arena->fill + padded <= arena->capacity);
    
    void* rv = arena->curr + arena->fill;
    arena->fill += padded;
    return rv;
}

static void cd_pp_arena_free(cd_pp_arena_t* arena)
{
    uint8_t* page = arena->first;
    while(page != NULL) {
        uint8_t* next = *(uint8_t**)page;
        free(page);
        page = next;
    }
    *arena = (cd_pp_arena_t){0};
}

static size_t cd_pp_hash_size_t(size_t value)
{
    value *= 0xff51afd7ed558ccd;
    value ^= value >> 32;
    return value;
}

static size_t cd_pp_map_get(const cd_pp_map_t* map, size_t key)
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

static void cd_pp_map_insert(cd_pp_map_t* map, size_t key, size_t val)
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

static void cd_pp_map_free(cd_pp_map_t* map)
{
    free(map->keys);
    free(map->vals);
    *map = (cd_pp_map_t){0};
}

static size_t cd_pp_fnv_1a(const char* begin, const char* end)
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

const char* cd_pp_str_intern(cd_pp_state_t* state, cd_pp_strview_t str)
{
    assert(state);
    assert(str.begin <= str.end);
    if(str.begin == NULL || str.begin == str.end) {
        return NULL;
    }
    size_t length = str.end - str.begin;

    uint64_t hash = cd_pp_fnv_1a(str.begin, str.end);
    hash = hash ? hash : 1; // hash should never be zero
    
    cd_pp_str_item_t* item = (cd_pp_str_item_t*)cd_pp_map_get(&state->str_map, hash);
    for(cd_pp_str_item_t* i = item; i != NULL; i = i->next) {
        if(i->length == length) {
            if(strncmp(i->str, str.begin, length)==0) {
                return i->str;
            }
        }
    }
    
    cd_pp_str_item_t* new_item = (cd_pp_str_item_t*)cd_pp_arena_alloc(state, &state->str_mem, sizeof(cd_pp_str_item_t) + length + 1);
    new_item->next = item;
    new_item->length = length;
    memcpy(new_item->str, str.begin, length);
    new_item->str[length] = '\0';
    cd_pp_map_insert(&state->str_map, hash, (size_t)new_item);
    return new_item->str;
}

void cd_pp_state_free(cd_pp_state_t* state)
{
    cd_pp_map_free(&state->str_map);
    cd_pp_arena_free(&state->str_mem);
}

bool cd_pp_process(cd_pp_state_t*           state,
                   cd_pp_strview_t          input,
                   cd_pp_handle_include_t   handle_include,
                   void*                    handle_data)
{
    cd_pp_ctx_t ctx = {
        .state = state,
        .input = input
    };

    while(cd_pp_next_token(&ctx) && ctx.current.kind != CD_PP_TOKEN_EOF) {
        
        CD_PP_LOG_DEBUG(state,
                        "token kind=%u '%.*s'",
                        ctx.current.kind,
                        (int)(ctx.current.text.end-ctx.current.text.begin),
                        ctx.current.text.begin);
    }
    return true;
}


#endif // of CD_PREPROC_IMPLEMENTATION

#ifdef __cplusplus
}   // extern "C"
#endif

#endif // of CD_PREPROC_H
