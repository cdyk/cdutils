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

typedef struct {
    const char* begin;
    const char* end;
} cd_pp_strview_t;

typedef struct cd_pp_state_t cd_pp_state_t;
typedef struct cd_pp_str_item_t cd_pp_str_item_t;
typedef struct cd_pp_token_t cd_pp_token_t;

typedef void (*cd_pp_log_func_t)(void* log_data, cd_pp_loglevel_t level, const char* msg, ...);
typedef bool (*cd_pp_output_func_t)(void* output_data, cd_pp_strview_t output);
typedef bool (*cd_pp_handle_include_t)(void* handler_data, cd_pp_state_t* state, cd_pp_strview_t path);


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

struct cd_pp_str_item_t {
    cd_pp_str_item_t*   next;
    size_t              length;
    char                str[0];
};


struct cd_pp_state_t {
    cd_pp_log_func_t        log_func;
    void*                   log_data;
    cd_pp_output_func_t     output_func;
    void*                   output_data;
    cd_pp_handle_include_t  handle_include;
    void*                   handle_data;
    cd_pp_map_t             str_map;
    cd_pp_arena_t           str_mem;
    unsigned                recursion_depth;
    struct {
        cd_pp_token_t*      data;
        size_t              size;
        size_t              capacity;
    } tmp_tokens;
};


const char* cd_pp_strview_intern(cd_pp_state_t* state, cd_pp_strview_t str);
const char* cd_pp_str_intern(cd_pp_state_t* state, const char*);

bool cd_pp_process(cd_pp_state_t*           state,
                   cd_pp_strview_t          input);

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

#define cd_pp_array_push(array, value) {                            \
    if((array)->capacity <= (array)->size) {                        \
        (array)->capacity = (array)->size ? 2*(array)->size : 16;   \
        *((void**)&(array)->data) = cd_pp_realloc((array)->data,    \
                                                sizeof((array)[0])* \
                                                (array)->capacity); \
    }                                                               \
    (array)->data[(array)->size++] = (value);                       \
}


typedef enum {
    CD_PP_TOKEN_EOF         = 0,
    CD_PP_TOKEN_HASH        = '#',  // 35
    CD_PP_TOKEN_LPARENS     = '(',  // 40
    CD_PP_TOKEN_RPARENS     = ')',  // 41
    CD_PP_TOKEN_BACKSLASH   = '\\', // 92
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
    CD_PP_TOKEN_NEWLINE,
    CD_PP_TOKEN_SUBST_SPACE         // Space in substitution rule
} cd_pp_token_kind_t;

struct  cd_pp_token_t {
    cd_pp_strview_t     text;
    cd_pp_token_kind_t  kind;
};

typedef struct {
    cd_pp_state_t*      state;
    cd_pp_strview_t     input;
    cd_pp_token_t       current;
    cd_pp_token_t       matched;
    const char*         id_define;
    const char*         id_include;
    const char*         id_undef;
    const char*         id_if;
    const char*         id_ifdef;
    const char*         id_ifndef;
    const char*         id_elif;
    const char*         id_else;
    const char*         id_endif;
    const char*         id_defined;
} cd_pp_ctx_t;


static bool cd_pp_next_token(cd_pp_ctx_t* ctx)
{
    ctx->matched = ctx->current;
start:
    if(ctx->input.end <= ctx->input.begin) {
        ctx->current.text = (cd_pp_strview_t){ctx->input.end,ctx->input.end};
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
               ('a' <= *ctx->input.begin && *ctx->input.begin <= 'z') ||
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

static bool cd_pp_is_token(cd_pp_ctx_t* ctx, cd_pp_token_kind_t kind)
{
    return ctx->current.kind == kind;
}

static bool cd_pp_match_token(cd_pp_ctx_t* ctx, cd_pp_token_kind_t kind)
{
    if(cd_pp_is_token(ctx, kind)) {
        cd_pp_next_token(ctx);
        return true;
    }
    return false;
}

static bool cd_pp_connected_tokens(cd_pp_ctx_t* ctx)
{
    return ctx->matched.text.end == ctx->current.text.begin;
}

static bool cd_pp_expect_token(cd_pp_ctx_t* ctx, cd_pp_token_kind_t kind, const char* expectation)
{
    if(cd_pp_is_token(ctx, kind)) {
        cd_pp_next_token(ctx);
        return true;
    }
    CD_PP_LOG_ERROR(ctx->state, "Expected %s, got '%.*s'",
                    expectation,
                    (int)(ctx->current.text.end - ctx->current.text.begin),
                    ctx->current.text.begin);
    return false;
}

static void cd_pp_match_until_newline(cd_pp_ctx_t* ctx)
{
    while(ctx->current.kind != CD_PP_TOKEN_NEWLINE &&
          ctx->current.kind != CD_PP_TOKEN_EOF)
    {
        cd_pp_next_token(ctx);
    }
}

static bool cd_pp_output_line(cd_pp_ctx_t* ctx, const char* line_start, bool active)
{
    cd_pp_match_until_newline(ctx);
    if(active && ctx->state->output_func) {
        if(!ctx->state->output_func(ctx->state->output_data,
                                    (cd_pp_strview_t){line_start, ctx->current.text.end}))
        {
            return false;
        }
    }
    return true;
}

static void* cd_pp_malloc(size_t size)
{
    void* rv = malloc(size);
    assert(rv != NULL);
    return rv;
}

static void* cd_pp_realloc(void* ptr, size_t size)
{
    void* rv = realloc(ptr, size);
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

static void cd_pp_strview_inplace_intern(cd_pp_state_t* state, cd_pp_strview_t* str)
{
    assert(state);
    assert(str->begin <= str->end);
    if(str->begin == NULL || str->begin == str->end) {
        str->begin = NULL;
        str->end = NULL;
        return;
    }
    size_t length = str->end - str->begin;

    uint64_t hash = cd_pp_fnv_1a(str->begin, str->end);
    hash = hash ? hash : 1; // hash should never be zero
    
    cd_pp_str_item_t* item = (cd_pp_str_item_t*)cd_pp_map_get(&state->str_map, hash);
    for(cd_pp_str_item_t* i = item; i != NULL; i = i->next) {
        if(i->length == length) {
            if(strncmp(i->str, str->begin, length)==0) {
                str->begin = i->str;
                str->end = i->str + length;
                return;
            }
        }
    }
    
    cd_pp_str_item_t* new_item = (cd_pp_str_item_t*)cd_pp_arena_alloc(state, &state->str_mem, sizeof(cd_pp_str_item_t) + length + 1);
    new_item->next = item;
    new_item->length = length;
    memcpy(new_item->str, str->begin, length);
    new_item->str[length] = '\0';
    cd_pp_map_insert(&state->str_map, hash, (size_t)new_item);

    str->begin = new_item->str;
    str->end = new_item->str + length;
}

const char* cd_pp_strview_intern(cd_pp_state_t* state, cd_pp_strview_t str)
{
    cd_pp_strview_inplace_intern(state, &str);
    return str.begin;
}

const char* cd_pp_str_intern(cd_pp_state_t* state, const char* str)
{
    cd_pp_strview_t t = {str, str+strlen(str)};
    cd_pp_strview_inplace_intern(state, &t);
    return t.begin;
}

static bool cd_pp_parse_define_args(cd_pp_ctx_t* ctx)
{
    // Currently ignore arguments as we don't support macros (yet?)
    while(!cd_pp_match_token(ctx, CD_PP_TOKEN_RPARENS)) {
        if(cd_pp_is_token(ctx, CD_PP_TOKEN_EOF)) {
            CD_PP_LOG_ERROR(ctx->state, "EOF while scanning for end of define argument.");
            return false;
        }
        else if(cd_pp_is_token(ctx, CD_PP_TOKEN_NEWLINE)) {
            CD_PP_LOG_ERROR(ctx->state, "Newline while scanning for end of define argument.");
            return false;
        }
        else if(cd_pp_is_token(ctx, CD_PP_TOKEN_LPARENS)) {
            CD_PP_LOG_ERROR(ctx->state, "Nested paranthesises in define argument.");
            return false;
        }
        cd_pp_next_token(ctx);
    }
    return true;
}

static bool cd_pp_parse_define(cd_pp_ctx_t* ctx, bool active)
{
    if(!active) {
        cd_pp_match_until_newline(ctx);
        return true;
    }
    
    if(!cd_pp_expect_token(ctx, CD_PP_TOKEN_IDENTIFIER, "No identifier after define")) return false;
    
    const char* name = cd_pp_strview_intern(ctx->state, ctx->matched.text);

    if(cd_pp_connected_tokens(ctx) && cd_pp_match_token(ctx, CD_PP_TOKEN_LPARENS)) {
        if(!cd_pp_parse_define_args(ctx)) return false;
    }
    
    const char* prev_tail = NULL;
    while(!cd_pp_is_token(ctx, CD_PP_TOKEN_EOF)) {
        if(cd_pp_match_token(ctx, CD_PP_TOKEN_BACKSLASH)) {
            if(!cd_pp_expect_token(ctx, CD_PP_TOKEN_NEWLINE, "Expected newline after macro line-splicing backslash")) return false;
            cd_pp_token_t newline = ctx->matched;
            cd_pp_strview_inplace_intern(ctx->state, &newline.text);
            cd_pp_array_push(&ctx->state->tmp_tokens, newline);
            prev_tail = ctx->matched.text.end;
        }
        else if(cd_pp_match_token(ctx, CD_PP_TOKEN_NEWLINE)) {
            break;
        }
        else {

            cd_pp_next_token(ctx);

            if(prev_tail != NULL) {
                assert(prev_tail <= ctx->matched.text.begin);
                cd_pp_token_t space = {
                    { prev_tail, ctx->matched.text.begin},
                    CD_PP_TOKEN_SUBST_SPACE
                };
                cd_pp_strview_inplace_intern(ctx->state, &space.text);
                cd_pp_array_push(&ctx->state->tmp_tokens, space);
            }
            
            cd_pp_token_t token = ctx->matched;
            cd_pp_strview_inplace_intern(ctx->state, &token.text);
            cd_pp_array_push(&ctx->state->tmp_tokens, token);
            prev_tail = ctx->matched.text.end;
        }
    }
    // add definition
    for(size_t i=0; i<ctx->state->tmp_tokens.size; i++) {
        CD_PP_LOG_DEBUG(ctx->state, "'%s'", ctx->state->tmp_tokens.data[i].text.begin);
    }
    return true;
}

static bool cd_pp_parse_text(cd_pp_ctx_t* ctx, bool active, bool expect_eof);

static bool cd_pp_parse_text_check(cd_pp_ctx_t* ctx, bool active, bool expect_eof)
{
    if(255 < ctx->state->recursion_depth) {
        CD_PP_LOG_ERROR(ctx->state, "Excessive recursion");
        return false;
    }
    ctx->state->recursion_depth++;
    bool rv = cd_pp_parse_text(ctx, active, expect_eof);
    ctx->state->recursion_depth--;
    return rv;
}

static bool cd_pp_parse_if_elif_else(cd_pp_ctx_t* ctx, bool active, bool expect_eof)
{
    bool can_match = active;
    const char* id = NULL;
    do {
        // parse const_expr
        cd_pp_match_until_newline(ctx);
        if(!cd_pp_parse_text_check(ctx, active, false)) return false;
        assert(ctx->matched.kind == CD_PP_TOKEN_IDENTIFIER);
        id = cd_pp_strview_intern(ctx->state, ctx->matched.text);
    }
    while(id == ctx->id_elif);
    
    if(id == ctx->id_else) {
        cd_pp_match_until_newline(ctx);
        if(!cd_pp_parse_text_check(ctx, active, false)) return false;
        assert(ctx->matched.kind == CD_PP_TOKEN_IDENTIFIER);
        id = cd_pp_strview_intern(ctx->state, ctx->matched.text);
    }
    
    if(id == ctx->id_endif) {
        cd_pp_match_until_newline(ctx);
    }
    else {
        CD_PP_LOG_ERROR(ctx->state, "Expected #endif, got #%s", id);
        return false;
    }
    return true;
}

static bool cd_pp_parse_text(cd_pp_ctx_t* ctx, bool active, bool expect_eof)
{
    while(cd_pp_next_token(ctx)) {

        bool forward = active;
        const char* line_start = ctx->current.text.begin;
        
        if(cd_pp_match_token(ctx, CD_PP_TOKEN_HASH)) {
            if(!cd_pp_expect_token(ctx, CD_PP_TOKEN_IDENTIFIER, "Expected identifier after #")) return false;
            const char* id = cd_pp_strview_intern(ctx->state, ctx->matched.text);

            if(id == ctx->id_define) {
                if(!cd_pp_parse_define(ctx, active)) return false;
            }
            
            if(id == ctx->id_elif ||
               id == ctx->id_else ||
               id == ctx->id_endif)
            {
                if(expect_eof) {
                    CD_PP_LOG_ERROR(ctx->state, "Expected EOF, got #%s", id);
                    return false;
                }
                return true;
            }
            else if(id == ctx->id_if) {
                if(!cd_pp_parse_if_elif_else(ctx, active, expect_eof)) return false;
            }
            else {
                if(!cd_pp_output_line(ctx, line_start, forward)) return false;
            }
        }
        else {
            if(!cd_pp_output_line(ctx, line_start, forward)) return false;
        }
        if(ctx->current.kind == CD_PP_TOKEN_EOF) {
            if(!expect_eof) {
                CD_PP_LOG_ERROR(ctx->state, "Unexpected EOF");
            }
            return expect_eof;
        }
    }
    return true;
}

void cd_pp_state_free(cd_pp_state_t* state)
{
    free(state->tmp_tokens.data);
    cd_pp_map_free(&state->str_map);
    cd_pp_arena_free(&state->str_mem);
}

bool cd_pp_process(cd_pp_state_t*           state,
                   cd_pp_strview_t          input)
{
    cd_pp_ctx_t ctx = {
        .state = state,
        .input = input,
        .id_define = cd_pp_str_intern(state, "define"),
        .id_include = cd_pp_str_intern(state, "include"),
        .id_undef = cd_pp_str_intern(state, "undef"),
        .id_if = cd_pp_str_intern(state, "if"),
        .id_ifdef = cd_pp_str_intern(state, "ifdef"),
        .id_ifndef = cd_pp_str_intern(state, "ifndef"),
        .id_elif = cd_pp_str_intern(state, "elif"),
        .id_else = cd_pp_str_intern(state, "else"),
        .id_endif = cd_pp_str_intern(state, "endif"),
        .id_defined = cd_pp_str_intern(state, "defined")
    };
    return cd_pp_parse_text(&ctx, true, true);
}


#endif // of CD_PREPROC_IMPLEMENTATION

#ifdef __cplusplus
}   // extern "C"
#endif

#endif // of CD_PREPROC_H
