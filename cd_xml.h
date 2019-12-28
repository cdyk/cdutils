#ifndef CD_XML_H
#define CD_XML_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

typedef uint32_t cd_xml_ns_ix_t;

typedef uint32_t cd_xml_att_ix_t;

typedef uint32_t cd_xml_node_ix_t;

static const uint32_t cd_xml_no_ix = (uint32_t)-1;

// Callback function for consuming output from writer
typedef bool (*cd_xml_output_func)(void* userdata, const char* ptr, size_t bytes);


// Stretchy-buf helpers
#define cd_xml__sb_base(a) ((unsigned*)(a)-2)
#define cd_xml__sb_size(a) (cd_xml__sb_base(a)[0])

// Get count of a stretchy-buf variable
#define cd_xml_sb_size(a) ((a)?cd_xml__sb_size(a):0u)


typedef struct {
    const char* begin;
    const char* end;
} cd_xml_stringview_t;

typedef struct {
    cd_xml_stringview_t name;
    cd_xml_stringview_t value;
    cd_xml_ns_ix_t ns;
    cd_xml_att_ix_t next_attribute;
} cd_xml_attribute_t;

typedef enum {
    CD_XML_NODE_ELEMENT,
    CD_XML_NODE_TEXT
} cd_xml_node_kind_t;

typedef enum
{
    CD_XML_STATUS_SUCCESS = 0,
    CD_XML_STATUS_POINTER_NOT_NULL,
    CD_XML_STATUS_UNKNOWN_NAMESPACE_PREFIX,
    CD_XML_STATUS_UNSUPPORTED_VERSION,
    CD_XML_STATUS_UNSUPPORTED_ENCODING,
    CD_XML_STATUS_MALFORMED_UTF8,
    CD_XML_STATUS_MALFORMED_ATTRIBUTE,
    CD_XML_STATUS_PREMATURE_EOF,
    CD_XML_STATUS_MALFORMED_DECLARATION,
    CD_XML_STATUS_UNEXPECTED_TOKEN,
    CD_XML_STATUS_MALFORMED_ENTITY
} cd_xml_parse_status_t;

// Holds data of a node, that is an element or text

typedef struct {                                            // Element data
    cd_xml_stringview_t name;                               // Element name.
    cd_xml_ns_ix_t      namespace_ix;                       // Element index, cd_xml_no_ix for no namespace.
    cd_xml_node_ix_t    first_child;                        // Pointer to first child node of this element.
    cd_xml_node_ix_t    last_child;                         // Pointer to last child node of this element.
    cd_xml_att_ix_t     first_attribute;                    // Pointer to first attribute of this element.
    cd_xml_att_ix_t     last_attribute;                     // Pointer to last attribute of this element.
} node_element_t;

typedef struct {                                            // Text data
    cd_xml_stringview_t contents;                           // Text contents
} node_text_t;

typedef struct {
    union {
        node_element_t          element;
        node_text_t             text;
    }                           data;
    cd_xml_node_ix_t            next_sibling;               // Next node sibling of same parent.
    cd_xml_node_kind_t          kind;                       // Kind of node, either CD_XML_NODE_ELEMENT or CD_XML_NODE_TEXT.
} cd_xml_node_t;

// Represents a namespace
typedef struct  {
    cd_xml_stringview_t         prefix;                     // Prefix of namespace, empty for default namespace.
    cd_xml_stringview_t         uri;                        // URI of namespace.
} cd_xml_ns_t;

// Header of memory allocated, stored in a linked list out of cd_xml_doc._t.allocated_buffers.
typedef struct cd_xml_buf_struct {
    struct cd_xml_buf_struct*   next;                       // Next allocated buffer or NULL.
    char                        payload;                    // Offset of payload data.
} cd_xml_buf_t;

// XML DOM representation
typedef struct {
    cd_xml_ns_t*                namespaces;                 // Array of namespaces, stretchy buf, count using cd_xml_sb_size.
    cd_xml_node_t*              nodes;                      // Array of nodes, stretchy buf, count using cd_xml_sb_size.
    cd_xml_attribute_t*         attributes;                 // Array of attributes, stretchy buf, count usng cd_xml_sb_size.
    cd_xml_buf_t*               allocated_buffers;          // Backing for modifieds strings.
} cd_xml_doc_t;

cd_xml_doc_t* cd_xml_init(void);

void cd_xml_free(cd_xml_doc_t** doc);

cd_xml_att_ix_t cd_xml_add_namespace(cd_xml_doc_t*          doc,
                                     cd_xml_stringview_t*   prefix,
                                     cd_xml_stringview_t*   uri);

cd_xml_node_ix_t cd_xml_add_element(cd_xml_doc_t*           doc,
                                    cd_xml_ns_ix_t          ns,
                                    cd_xml_stringview_t*    name,
                                    cd_xml_node_ix_t        parent);

cd_xml_att_ix_t cd_xml_add_attribute(cd_xml_doc_t*          doc,
                                     cd_xml_ns_ix_t         ns,
                                     cd_xml_stringview_t*   name,
                                     cd_xml_stringview_t*   value,
                                     cd_xml_node_ix_t       element);

cd_xml_node_ix_t cd_xml_add_text(cd_xml_doc_t*              doc,
                                 cd_xml_stringview_t*       content,
                                 cd_xml_node_ix_t           parent);

cd_xml_parse_status_t cd_xml_init_and_parse(cd_xml_doc_t**  doc,
                                            const char*     data,
                                            size_t          size);

bool cd_xml_write(cd_xml_doc_t*         doc,
                  cd_xml_output_func    output_func,
                  void*                 userdata,
                  bool                  pretty);

#ifdef CD_XML_IMPLEMENTATION

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <stdarg.h>

// Define CD_XML_MALLOC, CD_XML_FREE, and CD_XML_REALLOC for custom allocation

#ifndef CD_XML_MALLOC
#define CD_XML_MALLOC(size) malloc(size)
#endif

#ifndef CD_XML_FREE
#define CD_XML_FREE(size) free(size)
#endif

#ifndef CD_XML_REALLOC
#define CD_XML_REALLOC(ptr,size) realloc(ptr,size)
#endif

// Recognized token types
typedef enum {
    CD_XML_TOKEN_EOF                    = 0,
    CD_XML_TOKEN_QUOTE                  = '"',              // ascii 34
    CD_XML_TOKEN_AMP                    = '&',              // ascii 38
    CD_XML_TOKEN_APOSTROPHE             = '\'',             // ascii 39
    CD_XML_TOKEN_COLON                  = ':',              // ascii 58
    CD_XML_TOKEN_TAG_START              = '<',              // ascii 60
    CD_XML_TOKEN_EQUAL                  = '=',              // ascii 61
    CD_XML_TOKEN_TAG_END                = '>',              // ascii 62
    CD_XML_TOKEN_LASTCHAR               = 127,
    CD_XML_TOKEN_NAME,
    CD_XML_TOKEN_EMPTYTAG_END,                              // />
    CD_XML_TOKEN_ENDTAG_START,                              // </
    CD_XML_TOKEN_PROC_INSTR_START,                          // <?
    CD_XML_TOKEN_PROC_INSTR_STOP,                           // ?>
    CD_XML_TOKEN_XML_DECL_START                             // <?xml
} cd_xml_token_kind_t;

// Decoded token
typedef struct {
    cd_xml_stringview_t         text;                       // Span in input buffer of token.
    cd_xml_token_kind_t         kind;                       // Token kind.
} cd_xml_token_t;

// Character decoding state
typedef struct {
    cd_xml_stringview_t         text;                       // Span in input buffer of current character.
    uint32_t                    code;                       // UTF-32 encoding of current character.
} cd_xml_chr_state_t;

// Stashed parsed attribute, used by cd_xml_parse_context_t.attribute_stash.
typedef struct {
    cd_xml_stringview_t         namespace;                  // Namespace of attribute.
    cd_xml_stringview_t         name;                       // Name of attribute.
    cd_xml_stringview_t         value;                      // Value of attribute.
} cd_xml_att_triple_t;

// Binding between a prefix and a namespace, used cd_xml_parse_context_t.namespace_resolve_stack.
typedef struct {
    cd_xml_stringview_t         prefix;                     // Prefix of namespace binding.
    cd_xml_ns_ix_t              namespace_ix;               // Index of bound namespace.
} cd_xml_namespace_binding_t;

// State used during parsing
typedef struct {
    cd_xml_doc_t*               doc;                        // Document that gets built during parsing.
    cd_xml_stringview_t         input;                      // Input buffer.
    cd_xml_chr_state_t          chr;                        // Current character.
    cd_xml_token_t              current;                    // Current token.
    cd_xml_token_t              matched;                    // Most recently matched token.
    cd_xml_att_triple_t*        attribute_stash;            // Temp stash used when parsing attributes.
    cd_xml_ns_ix_t              namespace_default;          // Current default namespace.
    cd_xml_namespace_binding_t* namespace_resolve_stack;    // Namespace-prefix bindings, most recent bindings last.
    cd_xml_parse_status_t       status;                     // Either success or first error encountered.
} cd_xml_parse_context_t;

#define CD_XML_MIN(a,b) ((a)<(b)?(a):(b))
#define CD_XML_MAX(a,b) ((a)<(b)?(b):(a))
#define CD_XML_STRINGVIEW_FORMAT(text) (int)((text).end-(text).begin),(text).begin
#define CD_XML_WRITE_HELPER(a) (a), strlen(a)
#define CD_XML_WRITE_HELPERV(a) (a).begin, (a).end-(a).begin

#define cd_xml_strv_empty(a) (a.begin == a.end)

// Stretchy bufs ala  https://github.com/nothings/stb/blob/master/stretchy_buffer.h

#define cd_xml__sb_cap(a) (cd_xml__sb_base(a)[1])
#define cd_xml__sb_must_grow(a) (((a)==NULL)||(cd_xml__sb_cap(a) <= cd_xml__sb_size(a)+1u))
#define cd_xml__sb_do_grow(a) (*(void**)&(a)=cd_xml__sb_grow((a),sizeof(*(a))))
#define cd_xml__sb_maybe_grow(a) (cd_xml__sb_must_grow(a)?cd_xml__sb_do_grow(a):0)
#define cd_xml_sb_push(a,x) (cd_xml__sb_maybe_grow(a),(a)[cd_xml__sb_size(a)++]=(x))
#define cd_xml_sb_shrink(a,n) ((a)&&((n)<cd_xml__sb_size(a))?cd_xml__sb_size(a)=(n):0)
#define cd_xml_sb_free(a) (cd_xml__sb_free((void**)&(a)))

static void cd_xml__sb_free(void** a) {
    if(*a) {
        CD_XML_FREE(cd_xml__sb_base(*a));
        *a = NULL;
    }
}

static void* cd_xml__sb_grow(void* ptr, size_t item_size)
{
    unsigned size = cd_xml_sb_size(ptr);
    unsigned new_size = 2 * size;
    if(new_size < 16) new_size = 16;
    unsigned* base = (unsigned*)CD_XML_REALLOC(ptr ? cd_xml__sb_base(ptr) : NULL, 2*sizeof(unsigned) + item_size * new_size);
    assert(base);
    base[0] = size;
    base[1] = new_size;
    return base + 2;
}

static void cd_xml_report_error(cd_xml_parse_context_t* ctx, const char* a, const char* b, const char* fmt, ...)
{
    assert(a <= b);
    assert(ctx->input.begin <= a);
    assert(b <= ctx->input.end);

    unsigned width = 10;
    const char* aa = a - width < ctx->input.begin ? ctx->input.begin : a - width;
    const char* bb = ctx->input.end < b + width ? ctx->input.end : b + width;

    if (width < b - a) b = a + width;

    fprintf(stderr, "%.*s\n", (int)(bb - aa), aa);
    for (ptrdiff_t i = 0; i < a - aa; i++) fputc('-', stderr);
    for (ptrdiff_t i = 0; i < b - a; i++) fputc('^', stderr);
    fputc('\n', stderr);

    va_list args;
    va_start(args, fmt);
    vprintf(fmt, args);
    va_end(args);
    fputc('\n', stderr);
}

static void cd_xml_report_debug(cd_xml_parse_context_t* ctx, const char* fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    vprintf(fmt, args);
    va_end(args);
    fputc('\n', stderr);
}

static bool cd_xml_strcmp(cd_xml_stringview_t* a, const char* b)
{
    size_t n = a->end - a->begin;
    return (strncmp(a->begin, b, n) == 0) && (b[n] == '\0');
}

static bool cd_xml_strvcmp(cd_xml_stringview_t* a,cd_xml_stringview_t* b)
{
    size_t na = a->end - a->begin;
    size_t nb = b->end - b->begin;
    return (na == nb) && (memcmp(a->begin, b->begin, na) == 0);
}

static bool cd_xml_isspace(uint32_t c)
{
    switch (c) {
    case ' ':
    case '\t':
    case '\n':
    case '\r':
    case '\v':
    case '\f':
        return true;
    default:
        return false;
    }
}

static bool cd_xml_is_name_char(uint32_t c)
{
    switch (c) {
    case '0': case '1': case '2': case '3': case '4': case '5': case '6':
    case '7': case '8': case '9':
    case 'a': case 'b': case 'c': case 'd': case 'e': case 'f': case 'g':
    case 'h': case 'i': case 'j': case 'k': case 'l': case 'm': case 'n':
    case 'o': case 'p': case 'q': case 'r': case 's': case 't': case 'u':
    case 'v': case 'w': case 'x': case 'y': case 'z':
    case 'A': case 'B': case 'C': case 'D': case 'E': case 'F': case 'G':
    case 'H': case 'I': case 'J': case 'K': case 'L': case 'M': case 'N':
    case 'O': case 'P': case 'Q': case 'R': case 'S': case 'T': case 'U':
    case 'V': case 'W': case 'X': case 'Y': case 'Z':
    case '.': case '-': case '_':
        return true;
    default:
        return false;
    }
}

static void cd_xml_consume_utf8(cd_xml_parse_context_t* ctx, unsigned bytes)
{
    for(unsigned i=1; i<bytes; i++) {
        if(ctx->chr.text.end == ctx->input.end || *ctx->chr.text.end == '\0') {
            ctx->status = CD_XML_STATUS_MALFORMED_UTF8;
            ctx->chr.code = 0;
            cd_xml_report_error(ctx, ctx->chr.text.begin, ctx->chr.text.end, "EOF inside %u-byte UTF-8 encoding", bytes);
            return;
        }
        unsigned code = (unsigned char)(*ctx->chr.text.end++);
        if((code & 0xc0) != 0x80) {
            ctx->status = CD_XML_STATUS_MALFORMED_UTF8;
            ctx->chr.code = 0;
            cd_xml_report_error(ctx, ctx->chr.text.begin, ctx->chr.text.end, "Illegal byte %x inside %u-byte UTF-8 encoding", code, bytes);
            return;
        }
        ctx->chr.code = (ctx->chr.code << 6) | (code & 0x3f);
    }
}

static bool cd_xml_next_char(cd_xml_parse_context_t* ctx)
{
    ctx->chr.text.begin = ctx->chr.text.end;
    if(ctx->input.end <= ctx->chr.text.end || *ctx->chr.text.end == '\0') { // end-of-file
        ctx->chr.code = 0;
    }
    else if((*ctx->chr.text.end & 0x80) == 0) {      // 7-bit ASCII
        ctx->chr.code = (unsigned char)(*ctx->chr.text.end++);
    }
    else if ((*ctx->chr.text.end & 0xe0) == 0xc0) {  // xxx----- == 110----- -> 2-byte UTF-8
        ctx->chr.code = (unsigned char)(*ctx->chr.text.end++) & 0x1f;
        cd_xml_consume_utf8(ctx, 2);
    }
    else if ((*ctx->chr.text.end & 0xf0) == 0xe0) {  // xxxx---- == 1110---- -> 3-byte UTF-8
        ctx->chr.code = (unsigned char)(*ctx->chr.text.end++) & 0x0f;
        cd_xml_consume_utf8(ctx, 3);
    }
    else if ((*ctx->chr.text.end & 0xf8) == 0xf0) {  // xxxxx--- == 11110--- -> 4-byte UTF-8
        ctx->chr.code = (unsigned char)(*ctx->chr.text.end++) & 0x0f;
        cd_xml_consume_utf8(ctx, 4);
    }
    else {
        ctx->status = CD_XML_STATUS_MALFORMED_UTF8;
        ctx->chr.code = 0;
        cd_xml_report_error(ctx, ctx->chr.text.begin, ctx->chr.text.end, "Illegal UTF-8 start byte %x", (unsigned)*ctx->chr.text.end);
        return false;
    }
    return true;
}

static bool cd_xml_next_token(cd_xml_parse_context_t* ctx)
{
    if(ctx->status != CD_XML_STATUS_SUCCESS) return false;
    ctx->matched = ctx->current;

restart:
    ctx->current.text.begin = ctx->chr.text.begin;
    if(ctx->chr.code == '\0') {
        ctx->current.kind = CD_XML_TOKEN_EOF;
        goto done;
    }

    ctx->current.kind = (cd_xml_token_kind_t)ctx->chr.code;
    switch (ctx->chr.code) {
    case ' ':           // skip space
    case '\t':
    case '\n':
    case '\r':
    case '\v':
    case '\f':
        do { cd_xml_next_char(ctx); } while(cd_xml_isspace(ctx->chr.code));
        goto restart;
        break;
    case '/':
        cd_xml_next_char(ctx);
        if(ctx->chr.code == '>') {
            cd_xml_next_char(ctx);
            ctx->current.kind = CD_XML_TOKEN_EMPTYTAG_END;
        }
        break;
    case '<':
        ctx->current.kind = CD_XML_TOKEN_TAG_START;
        cd_xml_next_char(ctx);
        if(ctx->chr.code == '/') {
            cd_xml_next_char(ctx);
            ctx->current.kind = CD_XML_TOKEN_ENDTAG_START;
        }
        else if(ctx->chr.code == '?') {
            ctx->current.kind = CD_XML_TOKEN_PROC_INSTR_START;
            cd_xml_next_char(ctx);
            cd_xml_chr_state_t save = ctx->chr;
            if(ctx->chr.code == 'x') {
                cd_xml_next_char(ctx);
                if(ctx->chr.code == 'm') {
                    cd_xml_next_char(ctx);
                    if(ctx->chr.code == 'l') {
                        cd_xml_next_char(ctx);
                        ctx->current.kind = CD_XML_TOKEN_XML_DECL_START;
                        goto done;
                    }
                }
            }
            ctx->chr = save;
        }
        else if(ctx->chr.code == '!') {
            cd_xml_chr_state_t save = ctx->chr;
            cd_xml_next_char(ctx);
            if(ctx->chr.code == '-') {
                cd_xml_next_char(ctx);
                if(ctx->chr.code == '-') {  // XML comment
                    cd_xml_next_char(ctx);
                    while(ctx->chr.code) {  // Until --> or EOF
                        uint32_t code = ctx->chr.code;
                        cd_xml_next_char(ctx);
                        if(code == '-' && ctx->chr.code == '-') {
                            cd_xml_next_char(ctx);
                            if(ctx->chr.code == '>') {
                                cd_xml_next_char(ctx);
                                goto restart;
                            }
                        }
                    }
                    ctx->current.kind = CD_XML_TOKEN_EOF;
                    ctx->status = CD_XML_STATUS_PREMATURE_EOF;
                    ctx->current.text.end = ctx->chr.text.begin;
                    cd_xml_report_error(ctx, ctx->current.text.begin, ctx->current.text.end, "EOF while scanning for end of XML comment");
                    return false;
                }
            }
            ctx->chr = save;
        }
        break;
    case '?':
        cd_xml_next_char(ctx);
        if(ctx->chr.code == '>') {
            cd_xml_next_char(ctx);
            ctx->current.kind = CD_XML_TOKEN_PROC_INSTR_STOP;
        }
        break;
    case '0': case '1': case '2': case '3': case '4': case '5': case '6':
    case '7': case '8': case '9':
    case 'a': case 'b': case 'c': case 'd': case 'e': case 'f': case 'g':
    case 'h': case 'i': case 'j': case 'k': case 'l': case 'm': case 'n':
    case 'o': case 'p': case 'q': case 'r': case 's': case 't': case 'u':
    case 'v': case 'w': case 'x': case 'y': case 'z':
    case 'A': case 'B': case 'C': case 'D': case 'E': case 'F': case 'G':
    case 'H': case 'I': case 'J': case 'K': case 'L': case 'M': case 'N':
    case 'O': case 'P': case 'Q': case 'R': case 'S': case 'T': case 'U':
    case 'V': case 'W': case 'X': case 'Y': case 'Z':
    case '_':
        //cd_xml_next_char(ctx);
        ctx->current.kind = CD_XML_TOKEN_NAME;
        do { cd_xml_next_char(ctx); } while(cd_xml_is_name_char(ctx->chr.code));
        break;
    default:
        cd_xml_next_char(ctx);
        break;
    }
done:
    ctx->current.text.end = ctx->chr.text.begin;
    return true;
}

static bool cd_xml_match_token(cd_xml_parse_context_t* ctx, cd_xml_token_kind_t token_kind)
{
    if(ctx->status == CD_XML_STATUS_SUCCESS && ctx->current.kind == token_kind) {
        if (cd_xml_next_token(ctx)) {
            return true;
        }
    }
    return false;
}

static bool cd_xml_expect_token(cd_xml_parse_context_t* ctx, cd_xml_token_kind_t token_kind, const char* msg)
{
    if (ctx->status == CD_XML_STATUS_SUCCESS) {
        if (ctx->current.kind == token_kind) {
            if (cd_xml_next_token(ctx)) {
                return true;
            }
        }
    }
    ctx->status = CD_XML_STATUS_UNEXPECTED_TOKEN;
    cd_xml_report_error(ctx, ctx->current.text.begin, ctx->current.text.end, msg);
    return false;
}

static char* cd_xml_alloc_buf(cd_xml_doc_t* doc, size_t bytes)
{
    cd_xml_buf_t* buf = (cd_xml_buf_t*)CD_XML_MALLOC(offsetof(cd_xml_buf_t, payload) + bytes);
    buf->next = doc->allocated_buffers;
    doc->allocated_buffers = buf;

    return &buf->payload;
}

static bool cd_xml_decode_entities(cd_xml_parse_context_t* ctx,
                                   cd_xml_stringview_t* out,
                                   cd_xml_stringview_t in,
                                   unsigned amps)
{
    if (amps == 0) {
        *out = in;
        return true;
    }
  
    ptrdiff_t size = in.end - in.begin;
    char* begin = cd_xml_alloc_buf(ctx->doc, size);
    char* end = begin;
    while(in.begin < in.end) {
        if(*in.begin == '&') {
            const char* entity_start = in.begin;
            in.begin++;
            if(in.begin < in.end && *in.begin == '#') {
                in.begin++;
                uint32_t code = 0;
                if(in.begin < in.end && *in.begin == 'x') { // hex-code entity
                    in.begin++;
                    while(in.begin < in.end && *in.begin != ';') {
                        unsigned c = (unsigned char)*in.begin++;
                        code = code << 4;
                        if(('0' <= c) && (c <= '9')) {
                            code += c - '0';
                        }
                        else if(('a' <= c) && (c <= 'f')) {
                            code += c - 'a' + 10;
                        }
                        else if(('A' <= c) && (c <= 'F')) {
                            code += c - 'A' + 10;
                        }
                        else {
                            ctx->status = CD_XML_STATUS_MALFORMED_ENTITY;
                            cd_xml_report_error(ctx, entity_start, in.begin, "Illegal hexidecimal digit %c in entity", c);
                            return false;
                        }
                    }
                }
                else {  // decimal code entity
                    while(in.begin < in.end && *in.begin != ';') {
                        unsigned c = (unsigned char)*in.begin++;
                        code = 10u * code;
                        if(('0' <= c) && (c <= '9')) {
                            code += c - '0';
                        }
                        else {
                            ctx->status = CD_XML_STATUS_MALFORMED_ENTITY;
                            cd_xml_report_error(ctx, entity_start, in.begin, "Illegal decimal digit %c in entity", c);
                            return false;
                        }
                    }
                }
                // Produce UTF-8 of code
                if(code <= 0x7f) {
                    assert(end - begin < size);
                    *end++ = code;
                }
                else if(code <= 0x7ff) {
                    assert(end - begin + 1 < size);
                    *end++ = (code >> 6  ) | 0xc0;
                    *end++ = (code & 0x3f) | 0x80;
                }
                else if(code <= 0xffff) {
                    assert(end - begin + 2 < size);
                    *end++ = ( code >> 12        ) | 0xe0;
                    *end++ = ((code >>  6) & 0x3f) | 0x80;
                    *end++ = ( code        & 0x3f) | 0x80;
                }
                else if(code <= 0x10ffff) {
                    assert(end - begin + 3 < size);
                    *end++ = ( code >> 18        ) | 0xf0;
                    *end++ = ((code >> 12) & 0x3f) | 0x80;
                    *end++ = ((code >>  6) & 0x3f) | 0x80;
                    *end++ = ( code        & 0x3f) | 0x80;
                }
                else {
                    ctx->status = CD_XML_STATUS_MALFORMED_ENTITY;
                    cd_xml_report_error(ctx, entity_start, in.begin, "Entity code %x too large for UTF-8 encoding", code);
                    return false;
                }
            }
            else {  // named entity
                cd_xml_stringview_t e = { .begin = in.begin };
                while(in.begin < in.end && *in.begin != ';') { in.begin++; }
                e.end = in.begin;

                if(cd_xml_strcmp(&e, "quot")) {
                    assert(end - begin < size);
                    *end++ = '"';
                }
                else if(cd_xml_strcmp(&e, "amp")) {
                    assert(end - begin < size);
                    *end++ = '&';
                }
                else if(cd_xml_strcmp(&e, "apos")) {
                    assert(end - begin < size);
                    *end++ = '\'';
                }
                else if(cd_xml_strcmp(&e, "lt")) {
                    assert(end - begin < size);
                    *end++ = '<';
                }
                else if(cd_xml_strcmp(&e, "gt")) {
                    assert(end - begin < size);
                    *end++ = '>';
                }
                else {
                    ctx->status = CD_XML_STATUS_MALFORMED_ENTITY;
                    cd_xml_report_error(ctx, entity_start, in.begin, "Unrecognized named entity '%.*s'", CD_XML_STRINGVIEW_FORMAT(e));
                    return false;
                }
            }
            
            if(*in.begin == *in.end || *in.begin != ';') {
                ctx->status = CD_XML_STATUS_MALFORMED_ENTITY;
                cd_xml_report_error(ctx, entity_start, in.begin, "Failed to find terminating ; of entity");
                return false;

            }
            in.begin++;
        }
        else {
            assert(end - begin < size);
            *end++ = *in.begin++;
        }
    }
    
    out->begin = begin;
    out->end = end;
    return true;
}

static bool cd_xml_parse_attribute_value(cd_xml_parse_context_t* ctx, cd_xml_stringview_t* out)
{
    cd_xml_token_kind_t delimiter = ctx->current.kind;
    if((delimiter != CD_XML_TOKEN_QUOTE) && (delimiter != CD_XML_TOKEN_APOSTROPHE)) {
        ctx->status = CD_XML_STATUS_UNEXPECTED_TOKEN;
        cd_xml_report_error(ctx, ctx->current.text.begin, ctx->current.text.end, "Expected attribute value enclosed by either ' or \"");
        return false;
    }
    unsigned amps = 0;
    cd_xml_stringview_t in = ctx->chr.text;
    while(ctx->chr.code && (ctx->chr.code != delimiter)) {
        if(ctx->chr.code == '&') { amps++; }
        if (!cd_xml_next_char(ctx)) return false;
    }
    if(ctx->chr.code == '\0') {
        ctx->status = CD_XML_STATUS_PREMATURE_EOF;
        cd_xml_report_error(ctx, ctx->current.text.begin, ctx->chr.text.end, "Expected attribute value enclosed by either ' or \"");
        return false;
    }
    in.end = ctx->chr.text.begin;
    if (cd_xml_next_char(ctx)) {
        if (cd_xml_next_token(ctx)) {
            if (cd_xml_decode_entities(ctx, out, in, amps)) {
                return true;
            }
        }
    }
    return false;
}

static bool cd_xml_parse_xml_decl(cd_xml_parse_context_t* ctx, bool is_decl)
{
    assert(ctx->matched.kind == is_decl ? CD_XML_TOKEN_XML_DECL_START : CD_XML_TOKEN_PROC_INSTR_START);
    cd_xml_stringview_t match = { .begin = ctx->matched.text.begin };
    while(ctx->status == CD_XML_STATUS_SUCCESS) {
        
        if(cd_xml_match_token(ctx, CD_XML_TOKEN_NAME)) {
            const char* att_begin = ctx->matched.text.begin;
            cd_xml_stringview_t name = ctx->matched.text;
            if(!cd_xml_expect_token(ctx, CD_XML_TOKEN_EQUAL, "Expected '='")) return false;
            cd_xml_stringview_t value;
            if (!cd_xml_parse_attribute_value(ctx, &value)) return false;

            if(is_decl) {

                if(cd_xml_strcmp(&name, "version")) {
                    if(cd_xml_strcmp(&value, "1.0")) { }
                    else {
                        ctx->status = CD_XML_STATUS_UNSUPPORTED_VERSION;
                        cd_xml_report_error(ctx, att_begin, ctx->chr.text.begin, "Unsupported xml version %.*s", CD_XML_STRINGVIEW_FORMAT(value));
                        return false;
                    }
                }
                else if(cd_xml_strcmp(&name, "encoding")) {
                    if(cd_xml_strcmp(&value, "ASCII")) { }
                    if(cd_xml_strcmp(&value, "UTF-8")) { }
                    else {
                        ctx->status = CD_XML_STATUS_UNSUPPORTED_ENCODING;
                        cd_xml_report_error(ctx, att_begin, ctx->chr.text.begin, "Unsupported encoding %.*s", CD_XML_STRINGVIEW_FORMAT(value));
                        return false;
                    }
                }
                else if(cd_xml_strcmp(&name, "standalone")) { /* ignore for now */ }
                else {
                    ctx->status = CD_XML_STATUS_MALFORMED_DECLARATION;
                    cd_xml_report_error(ctx, att_begin, ctx->chr.text.begin,
                                        "Unrecognized declaration attribute '%.*s'='%.*s'",
                                        CD_XML_STRINGVIEW_FORMAT(name),
                                        CD_XML_STRINGVIEW_FORMAT(value));
                    return false;
                }
            }
        }
        else if(cd_xml_match_token(ctx, CD_XML_TOKEN_PROC_INSTR_STOP)) {
            match.end = ctx->matched.text.end;
            if (is_decl == false) {
                cd_xml_report_debug(ctx, "Skipped xml proc inst '%.*s'", CD_XML_STRINGVIEW_FORMAT(match));
            }
            return true;
        }
        else if(cd_xml_match_token(ctx, CD_XML_TOKEN_EOF)) {
            ctx->status = is_decl ? CD_XML_STATUS_MALFORMED_DECLARATION : CD_XML_STATUS_PREMATURE_EOF;

            cd_xml_report_error(ctx, match.begin,
                                ctx->chr.text.begin + 20 < ctx->input.end ? ctx->chr.text.begin + 20 : ctx->input.end,
                                "EOF while parsing %s", is_decl ? "xml decl" : "xml proc inst");
            return false;
        }
        else {
            cd_xml_next_token(ctx);
        }
    }

    return true;
}

static bool cd_xml_parse_prolog(cd_xml_parse_context_t* ctx)
{
    if(cd_xml_match_token(ctx, CD_XML_TOKEN_XML_DECL_START)) {
        if (!cd_xml_parse_xml_decl(ctx, true)) return false;
    }
    while(cd_xml_match_token(ctx, CD_XML_TOKEN_PROC_INSTR_START)) {
        if (!cd_xml_parse_xml_decl(ctx, false)) return false;
    }
    return true;
}

static bool cd_xml_parse_attribute(cd_xml_parse_context_t* ctx)
{
    assert(ctx->matched.kind == CD_XML_TOKEN_NAME);

    cd_xml_stringview_t ns = { NULL, NULL };
    cd_xml_stringview_t name = ctx->matched.text;
    if(cd_xml_match_token(ctx, CD_XML_TOKEN_COLON)) {
        if (!cd_xml_match_token(ctx, CD_XML_TOKEN_NAME)) {
            ctx->status = CD_XML_STATUS_UNEXPECTED_TOKEN;
            cd_xml_report_error(ctx, name.begin, ctx->matched.text.end, "Expected attribute name after ':'");
            return false;
        }
        ns = name;
        name = ctx->matched.text;
    }

    if (!cd_xml_match_token(ctx, CD_XML_TOKEN_EQUAL)) {
        ctx->status = CD_XML_STATUS_UNEXPECTED_TOKEN;
        cd_xml_report_error(ctx, name.begin, ctx->matched.text.end, "Expected '=' after attribute name");
        return false;
    }

    cd_xml_stringview_t value;
    if(!cd_xml_parse_attribute_value(ctx, &value)) return false;

    // Register namespace
    if(cd_xml_strcmp(&ns, "xmlns")) {
        if(cd_xml_strv_empty(name)) {
            ctx->status = CD_XML_STATUS_MALFORMED_ATTRIBUTE;
            cd_xml_report_error(ctx, name.begin, name.end, "Empty namespace prefix");
            return false;
        }
        else if(cd_xml_strv_empty(value)) {
            ctx->status = CD_XML_STATUS_MALFORMED_ATTRIBUTE;
            cd_xml_report_error(ctx, name.begin, ctx->chr.text.end, "Empty namespace uri");
            return false;
        }
        
        cd_xml_ns_ix_t ns = cd_xml_add_namespace(ctx->doc, &name, &value);
        
        cd_xml_namespace_binding_t binding = {
            .prefix = name,
            .namespace_ix = ns
        };
        cd_xml_sb_push(ctx->namespace_resolve_stack, binding);
        return ns != cd_xml_no_ix;
    }

    // Default namespace
    if(ns.begin == NULL && cd_xml_strcmp(&name, "xmlns")) {
        if(cd_xml_strv_empty(value)) {
            ctx->status = CD_XML_STATUS_MALFORMED_ATTRIBUTE;
            cd_xml_report_error(ctx, name.begin, ctx->chr.text.end, "Empty namespace uri");
            return false;
        }

        cd_xml_ns_ix_t namespace_ix = cd_xml_add_namespace(ctx->doc, NULL, &value);
        ctx->namespace_default = namespace_ix;
        return namespace_ix != cd_xml_no_ix;
    }
    
    cd_xml_att_triple_t att = {
        .namespace = ns,
        .name = name,
        .value = value
    };
    cd_xml_sb_push(ctx->attribute_stash, att);
    return true;
}

static bool cd_xml_parse_element_tag_start(cd_xml_parse_context_t* ctx, cd_xml_stringview_t* ns, cd_xml_stringview_t* name)
{
    assert(ctx->matched.kind == CD_XML_TOKEN_TAG_START);
    if(!cd_xml_expect_token(ctx, CD_XML_TOKEN_NAME, "Expected element name")) return false;

    ns->begin = NULL;
    ns->end = NULL;
    *name = ctx->matched.text;
    if(cd_xml_match_token(ctx, CD_XML_TOKEN_COLON)) {
        if (!cd_xml_match_token(ctx, CD_XML_TOKEN_NAME)) {
            ctx->status = CD_XML_STATUS_UNEXPECTED_TOKEN;
            cd_xml_report_error(ctx, name->begin, ctx->matched.text.end, "Expected element name after ':'");
            return false;
        }
        *ns = *name;
        *name = ctx->matched.text;
    }

    // parse attributes
    // get all namespaces defines before we start to resolve stuff
    while(cd_xml_match_token(ctx, CD_XML_TOKEN_NAME)) {
        if (!cd_xml_parse_attribute(ctx)) return false;
    }

    return true;
}

static bool cd_xml_parse_element(cd_xml_parse_context_t* ctx, cd_xml_node_ix_t parent);

static bool cd_xml_parse_element_contents(cd_xml_parse_context_t*   ctx,
                                          cd_xml_stringview_t*      elem_namespace,
                                          cd_xml_stringview_t*      elem_name,
                                          cd_xml_node_ix_t          parent)
{
    if(cd_xml_match_token(ctx, CD_XML_TOKEN_EMPTYTAG_END)) {
        // Leaf tag
        return true;
    }

    else if(!cd_xml_match_token(ctx, CD_XML_TOKEN_TAG_END)) {
        cd_xml_report_error(ctx, ctx->current.text.begin, ctx->current.text.end, "Expected either attribute name, > or />");
        ctx->status = CD_XML_STATUS_UNEXPECTED_TOKEN;
        return false;
    }

    unsigned amps = 0;
    cd_xml_stringview_t text = { NULL, NULL};
    const char* tag_start = ctx->matched.text.begin;

    while(ctx->status == CD_XML_STATUS_SUCCESS) {

        if(cd_xml_match_token(ctx, CD_XML_TOKEN_ENDTAG_START)) {
            if(!cd_xml_expect_token(ctx, CD_XML_TOKEN_NAME, "In end-tag, expected name")) return false;

            cd_xml_stringview_t name = ctx->matched.text;
            if(cd_xml_match_token(ctx, CD_XML_TOKEN_COLON)) {
                if(!cd_xml_strvcmp(&name, elem_namespace)) {
                    cd_xml_report_error(ctx, name.begin, name.end, "Namespace prefix mismatch");
                    ctx->status = CD_XML_STATUS_MALFORMED_ENTITY;
                    return false;
                }
                if(!cd_xml_expect_token(ctx, CD_XML_TOKEN_NAME, "Expected name after prefix")) return false;
                name = ctx->matched.text;
            }
            if(!cd_xml_strvcmp(&name, elem_name)) {
                cd_xml_report_error(ctx, name.begin, name.end, "Namespace prefix mismatch");
                ctx->status = CD_XML_STATUS_MALFORMED_ENTITY;
                return false;
            }

            if(!cd_xml_expect_token(ctx, CD_XML_TOKEN_TAG_END, "In end-tag, expected >")) return false;

            if(text.begin != NULL) {
                cd_xml_stringview_t decoded;
                if (!cd_xml_decode_entities(ctx, &decoded, text, amps)) return false;
                cd_xml_add_text(ctx->doc, &decoded, parent);
                text.begin = NULL;
            }
            break;
        }

        else if(cd_xml_match_token(ctx, CD_XML_TOKEN_TAG_START)) {

            if(text.begin != NULL) {
                cd_xml_stringview_t decoded;
                if (!cd_xml_decode_entities(ctx, &decoded, text, amps)) return false;
                cd_xml_add_text(ctx->doc, &decoded, parent);
                text.begin = NULL;
            }

            if(!cd_xml_parse_element(ctx, parent)) return false;

            if(ctx->status != CD_XML_STATUS_SUCCESS) return false;
        }

        else if(ctx->current.kind == CD_XML_TOKEN_EOF) {
            ctx->status = CD_XML_STATUS_PREMATURE_EOF;
            cd_xml_report_error(ctx, tag_start, ctx->chr.text.end,
                                "EOF while scanning for end of tag %.*s:%.*s",
                                CD_XML_STRINGVIEW_FORMAT(*elem_namespace),
                                CD_XML_STRINGVIEW_FORMAT(*elem_name));
            return false;
        }
        else {
            if(ctx->current.kind == CD_XML_TOKEN_AMP) {
                amps++;
            }
            if(text.begin == NULL) {
                text.begin = ctx->current.text.begin;
            }
            text.end = ctx->current.text.end;
            cd_xml_next_token(ctx);
        }
    }
    return true;
}


static bool cd_xml_resolve_namespace(cd_xml_parse_context_t*    ctx,
                                     cd_xml_ns_ix_t*            ns_ix,
                                     cd_xml_stringview_t*       prefix)
{
    // Note: Assumption here is that the number of namespaces are pretty low (1-3),
    // so a linear search suffices.
    unsigned n = cd_xml_sb_size(ctx->namespace_resolve_stack);
    for(unsigned i = n-1; i<n; i--) {
        if(cd_xml_strvcmp(&ctx->namespace_resolve_stack[i].prefix, prefix)) {
            *ns_ix = ctx->namespace_resolve_stack[i].namespace_ix;
            return true;
        }
    }
    ctx->status = CD_XML_STATUS_UNKNOWN_NAMESPACE_PREFIX;
    cd_xml_report_error(ctx, prefix->begin, prefix->end, "Unable to resolve namespace prefix");
    return false;
}

static bool cd_xml_parse_element(cd_xml_parse_context_t* ctx, cd_xml_node_ix_t parent)
{
    cd_xml_ns_ix_t parent_default_ns = ctx->namespace_default;
    unsigned parent_bind_stack_height = cd_xml_sb_size(ctx->namespace_resolve_stack);

    cd_xml_stringview_t elem_ns = { NULL, NULL };
    cd_xml_stringview_t elem_name = ctx->matched.text;

    if(cd_xml_parse_element_tag_start(ctx, &elem_ns, &elem_name)) {


        cd_xml_ns_ix_t elem_ns_ix = ctx->namespace_default;
        if(!cd_xml_strv_empty(elem_ns)) {
            if(!cd_xml_resolve_namespace(ctx, &elem_ns_ix, &elem_ns)) return false;
        }
        cd_xml_node_ix_t elem_ix = cd_xml_add_element(ctx->doc, elem_ns_ix, &elem_name, parent);

        for(unsigned i=0; i<cd_xml_sb_size(ctx->attribute_stash); i++) {
            cd_xml_att_triple_t* att = &ctx->attribute_stash[i];

            cd_xml_ns_ix_t att_ns_ix = cd_xml_no_ix;
            if(!cd_xml_strv_empty(att->namespace)) {
                if(!cd_xml_resolve_namespace(ctx, &att_ns_ix, &att->namespace)) return false;
            }

            
            cd_xml_add_attribute(ctx->doc,
                                 att_ns_ix,
                                 &att->name,
                                 &att->value,
                                 elem_ix);
        }


        if(cd_xml_parse_element_contents(ctx, &elem_ns, &elem_name, elem_ix)) {
            cd_xml_sb_shrink(ctx->namespace_resolve_stack, parent_bind_stack_height);
            ctx->namespace_default = parent_default_ns;
            return true;
        }
    }
    cd_xml_sb_shrink(ctx->namespace_resolve_stack, parent_bind_stack_height);
    ctx->namespace_default = parent_default_ns;
    return false;
}

cd_xml_att_ix_t cd_xml_add_namespace(cd_xml_doc_t* doc,
                                     cd_xml_stringview_t* prefix,
                                     cd_xml_stringview_t* uri)
{
    assert(uri->begin < uri->end && "URI cannot be empty");

    // Assume that number of namespaces are quite low, so
    // a linear search will do for now.
    for(unsigned i=0; i<cd_xml_sb_size(doc->namespaces); i++) {
        if(cd_xml_strvcmp(&doc->namespaces[i].uri, uri)) {
            // Match
            return i;
        }
    }
    // Register new namespace
    unsigned ix = cd_xml_sb_size(doc->namespaces);
    cd_xml_ns_t x = {0};
    if (prefix) {
        x.prefix = *prefix;
    }
    x.uri = *uri;
    cd_xml_sb_push(doc->namespaces, x);
    return ix;
}

cd_xml_node_ix_t cd_xml_add_text(cd_xml_doc_t* doc,
                                 cd_xml_stringview_t* content,
                                 cd_xml_node_ix_t parent)
{
    assert((parent != cd_xml_no_ix) && "Text node must have a parent");
    assert((cd_xml_sb_size(doc->nodes) != 0) && "Text node cannot be root");
    assert((doc->nodes[parent].kind == CD_XML_NODE_ELEMENT) && "Parent node of text must be an element");

    cd_xml_node_t element = {
        .next_sibling = cd_xml_no_ix,
        .data.text.contents = *content,
        .kind = CD_XML_NODE_TEXT
    };
    cd_xml_node_ix_t elem_ix = cd_xml_sb_size(doc->nodes);
    cd_xml_sb_push(doc->nodes, element);
    if (parent != cd_xml_no_ix) {
        if (doc->nodes[parent].data.element.first_child == cd_xml_no_ix) {    // first child of parent
            doc->nodes[parent].data.element.first_child = elem_ix;
            doc->nodes[parent].data.element.last_child = elem_ix;
        }
        else {
            doc->nodes[doc->nodes[parent].data.element.last_child].next_sibling = elem_ix;
            doc->nodes[parent].data.element.last_child = elem_ix;
        }
    }
    return elem_ix;
}


cd_xml_node_ix_t cd_xml_add_element(cd_xml_doc_t* doc,
                                    cd_xml_ns_ix_t ns,
                                    cd_xml_stringview_t* name,
                                    cd_xml_node_ix_t parent)
{
    assert(((parent != cd_xml_no_ix) || (cd_xml_sb_size(doc->nodes) == 0)) && "Root element must be the first element added to the document");
    assert(((parent == cd_xml_no_ix) || (parent < cd_xml_sb_size(doc->nodes))) && "Invalid parent index");

    cd_xml_node_t element = {
        .next_sibling = cd_xml_no_ix,
        .kind = CD_XML_NODE_ELEMENT,
        .data.element = {
            .first_child = cd_xml_no_ix,
            .last_child = cd_xml_no_ix,
            .name = *name,
            .namespace_ix = ns,
            .first_attribute = cd_xml_no_ix,
            .last_attribute = cd_xml_no_ix,
        }
    };
    cd_xml_node_ix_t elem_ix = cd_xml_sb_size(doc->nodes);
    cd_xml_sb_push(doc->nodes, element);
    if (parent != cd_xml_no_ix) {
        if (doc->nodes[parent].data.element.first_child == cd_xml_no_ix) {    // first child of parent
            doc->nodes[parent].data.element.first_child = elem_ix;
            doc->nodes[parent].data.element.last_child = elem_ix;
        }
        else {
            doc->nodes[doc->nodes[parent].data.element.last_child].next_sibling = elem_ix;
            doc->nodes[parent].data.element.last_child = elem_ix;
        }
    }
    return elem_ix;
}

cd_xml_att_ix_t cd_xml_add_attribute(cd_xml_doc_t* doc,
                                     cd_xml_ns_ix_t ns,
                                     cd_xml_stringview_t* name,
                                     cd_xml_stringview_t* value,
                                     cd_xml_node_ix_t element_ix)
{
    assert(name && "Name cannot be null");
    assert(value && "Value cannot be null");
    assert((ns == cd_xml_no_ix || ns < cd_xml_sb_size(doc->namespaces)) && "Illegal namespace index");
    assert((element_ix < cd_xml_sb_size(doc->nodes)) && "Illegal element index");

    cd_xml_att_ix_t att_ix = cd_xml_sb_size(doc->attributes);
    cd_xml_attribute_t att = {
        .name = *name,
        .value = *value,
        .ns = ns,
        .next_attribute = cd_xml_no_ix
    };
    cd_xml_sb_push(doc->attributes, att);

    cd_xml_node_t* elem = &doc->nodes[element_ix];
    assert(elem->kind == CD_XML_NODE_ELEMENT);
    
    if(elem->data.element.first_attribute == cd_xml_no_ix) {
        elem->data.element.first_attribute = att_ix;
        elem->data.element.last_attribute = att_ix;
    }
    else {
        doc->attributes[elem->data.element.last_attribute].next_attribute = att_ix;
        elem->data.element.last_attribute = att_ix;
    }
    return att_ix;
}


cd_xml_doc_t* cd_xml_init()
{
    cd_xml_doc_t* doc = CD_XML_MALLOC(sizeof(cd_xml_doc_t));
    memset(doc, 0, sizeof(cd_xml_doc_t));
    return doc;
}

void cd_xml_free(cd_xml_doc_t** doc)
{
    assert(doc);
    if (*doc == NULL) return;

    cd_xml_sb_free((*doc)->namespaces);
    cd_xml_sb_free((*doc)->nodes);
    cd_xml_sb_free((*doc)->attributes);
    cd_xml_buf_t* cb = (*doc)->allocated_buffers;
    while(cb) {
        cd_xml_buf_t* nb = cb->next;
        CD_XML_FREE(cb);
        cb = nb;
    }
    
    *doc = NULL;
}


cd_xml_parse_status_t cd_xml_init_and_parse(cd_xml_doc_t**  doc,
                                            const char*     data,
                                            size_t          size)
{
    if(*doc != NULL) {
        return CD_XML_STATUS_POINTER_NOT_NULL;
    }
    *doc = cd_xml_init();
    assert(*doc);
    
    cd_xml_parse_context_t ctx = {
        .doc = *doc,
        .input = {
            .begin = data,
            .end = data + size
        },
        .chr = {
            .text = {
                .end = data
            }
        },
        .namespace_default = cd_xml_no_ix,
        .status = CD_XML_STATUS_SUCCESS
    };
    
    if (cd_xml_next_char(&ctx) && cd_xml_next_token(&ctx)) {
        if(cd_xml_parse_prolog(&ctx)) {
            if(cd_xml_expect_token(&ctx, CD_XML_TOKEN_TAG_START, "Expected element start '<'")) {
                if(cd_xml_parse_element(&ctx, cd_xml_no_ix)) {
                    if(cd_xml_expect_token(&ctx, CD_XML_TOKEN_EOF, "Expexted EOF")) {
                        if(ctx.status == CD_XML_STATUS_SUCCESS) {
                            goto exit;
                        }
                    }
                }
            }
        }
    }

    cd_xml_free(doc);
    *doc = NULL;

exit:
    cd_xml_sb_free(ctx.attribute_stash);
    cd_xml_sb_free(ctx.namespace_resolve_stack);

    return ctx.status;
}

static bool cd_xml_encode_and_write(cd_xml_output_func      output_func,
                                    void*                   userdata,
                                    cd_xml_stringview_t*    text)
{
    const char* done = text->begin;
    for (const char* p = text->begin; p < text->end; p++) {
        const char* enc = NULL;
        switch (*p) {
        case '"':  enc = "&quot;"; break;
        case '&':  enc = "&amp;";  break;
        case '\'': enc = "&apos;"; break;
        case '<':  enc = "&lt;";   break;
        case '>':  enc = "&gt;";   break;
        default: break;
        }
        if (enc) {
            if (0 < p - done) {
                if (!output_func(userdata, done, p - done)) return false;
            }
            done = p + 1;
            if (!output_func(userdata, CD_XML_WRITE_HELPER(enc))) return false;
        }
    }
    if (0 < text->end - done) {
        if (!output_func(userdata, done, text->end - done)) return false;
    }
    return true;
}

static bool cd_xml_write_indent(cd_xml_doc_t*       doc,
                                cd_xml_output_func  output_func,
                                void*               userdata,
                                size_t              cols,
                                bool                needs_sep,
                                bool                pretty)
{
    static const char* indent = "\n                                        ";
    const size_t indent_l = strlen(indent);
    if (pretty) {
        if (!output_func(userdata, indent, CD_XML_MIN(cols + 1, indent_l))) return false;
    }
    else if(needs_sep) {
              if (!output_func(userdata, CD_XML_WRITE_HELPER(" "))) return false;
    }
    return true;
}

static bool cd_xml_write_namespace_defs(cd_xml_doc_t*       doc,
                                        cd_xml_output_func  output_func,
                                        void*               userdata,
                                        cd_xml_node_t*      elem,
                                        size_t              depth,
                                        bool                pretty)
{
    for (unsigned i = 0; i < cd_xml_sb_size(doc->namespaces); i++) {
        cd_xml_ns_t* ns = &doc->namespaces[i];

        if(!cd_xml_write_indent(doc,
                                output_func,
                                userdata,
                                2 * (size_t)depth + 2 + (elem->data.element.name.end - elem->data.element.name.begin),
                                true,
                                (i != 0) && pretty)) return false;
        
        if (cd_xml_strv_empty(ns->prefix)) {    // default namespace
            if (!output_func(userdata, CD_XML_WRITE_HELPER("xmlns=\""))) return false;
            if (!output_func(userdata, CD_XML_WRITE_HELPERV(ns->uri))) return false;
            if (!output_func(userdata, CD_XML_WRITE_HELPER("\""))) return false;
        }
        else {                                  // prefixed namespace
            if (!output_func(userdata, CD_XML_WRITE_HELPER("xmlns:"))) return false;
            if (!output_func(userdata, CD_XML_WRITE_HELPERV(ns->prefix))) return false;
            if (!output_func(userdata, CD_XML_WRITE_HELPER("=\""))) return false;
            if (!output_func(userdata, CD_XML_WRITE_HELPERV(ns->uri))) return false;
            if (!output_func(userdata, CD_XML_WRITE_HELPER("\""))) return false;
        }
    }

    return true;
}

static bool cd_xml_write_element_attributes(cd_xml_doc_t*       doc,
                                            cd_xml_output_func  output_func,
                                            void*               userdata,
                                            cd_xml_node_t*      elem,
                                            size_t              depth)
{
    for(cd_xml_att_ix_t att_ix = elem->data.element.first_attribute; att_ix != cd_xml_no_ix; att_ix = doc->attributes[att_ix].next_attribute) {
        cd_xml_attribute_t* att = &doc->attributes[att_ix];
        if(!output_func(userdata, CD_XML_WRITE_HELPER(" "))) return false;
        if(att->ns != cd_xml_no_ix) {
            assert(att->ns < cd_xml_sb_size(doc->attributes));
            if(!output_func(userdata, CD_XML_WRITE_HELPERV(doc->namespaces[att->ns].prefix))) return false;
            if(!output_func(userdata, CD_XML_WRITE_HELPER(":"))) return false;
        }
        if(!output_func(userdata, CD_XML_WRITE_HELPERV(att->name))) return false;
        if(!output_func(userdata, CD_XML_WRITE_HELPER("=\""))) return false;
        if(!cd_xml_encode_and_write(output_func, userdata, &att->value)) return false;
        if(!output_func(userdata, CD_XML_WRITE_HELPER("\""))) return false;
    }
    return true;
}

static bool cd_xml_write_element_name(cd_xml_doc_t*      doc,
                                      cd_xml_output_func output_func,
                                      void*              userdata,
                                      cd_xml_node_t*     elem)
{
    if (elem->data.element.namespace_ix != cd_xml_no_ix) {
        assert(elem->data.element.namespace_ix < cd_xml_sb_size(doc->namespaces));
        cd_xml_ns_t* ns = &doc->namespaces[elem->data.element.namespace_ix];
        if(!cd_xml_strv_empty(ns->prefix)) {    // empty -> default namespace
            if (!output_func(userdata, CD_XML_WRITE_HELPERV(ns->prefix))) return false;
            if (!output_func(userdata, CD_XML_WRITE_HELPER(":"))) return false;
        }
    }
    if (!output_func(userdata, CD_XML_WRITE_HELPERV(elem->data.element.name))) return false;
    return true;
}

static bool cd_xml_write_element(cd_xml_doc_t*      doc,
                                 cd_xml_output_func output_func,
                                 void*              userdata,
                                 cd_xml_node_ix_t   elem_ix,
                                 size_t             depth,
                                 bool               pretty)
{

    assert(elem_ix < cd_xml_sb_size(doc->nodes));
    cd_xml_node_t* elem = &doc->nodes[elem_ix];

    if(!cd_xml_write_indent(doc,
                            output_func,
                            userdata,
                            2 * depth,
                            false,
                            pretty)) return false;
    
    if (elem->kind == CD_XML_NODE_ELEMENT) {
        if (!output_func(userdata, "<", 1)) return false;

        if(!cd_xml_write_element_name(doc, output_func, userdata, elem)) return false;

        if (elem_ix == 0) {
            if(!cd_xml_write_namespace_defs(doc,
                                            output_func, userdata,
                                            elem, depth, pretty)) return false;
        }
        if(!cd_xml_write_element_attributes(doc,
                                            output_func, userdata,
                                            elem, depth)) return false;

        if (elem->data.element.first_child == cd_xml_no_ix) {
            if (!output_func(userdata, "/>", 2)) return false;
            return true;
        }
        else {
            if (!output_func(userdata, ">", 1)) return false;

            for (cd_xml_node_ix_t child_ix = elem->data.element.first_child; child_ix != cd_xml_no_ix; child_ix = doc->nodes[child_ix].next_sibling) {
                if (!cd_xml_write_element(doc, output_func, userdata, child_ix, depth + 1, pretty)) return false;
            }

            if(!cd_xml_write_indent(doc,
                                    output_func,
                                    userdata,
                                    2 * depth,
                                    false,
                                    pretty)) return false;

            if (!output_func(userdata, "<//", 2)) return false;
            if(!cd_xml_write_element_name(doc, output_func, userdata, elem)) return false;
            if (!output_func(userdata, ">", 1)) return false;
        }
    }
    else if (elem->kind == CD_XML_NODE_TEXT) {
        if (!cd_xml_encode_and_write(output_func, userdata, &elem->data.text.contents)) return false;
    }
    else {
        assert(0 && "Illegal elem kind");
    }
    return true;
}

bool cd_xml_write(cd_xml_doc_t* doc, cd_xml_output_func output_func, void* userdata, bool pretty)
{
    const char* decl = "<?xml version=\"1.0\" encoding=\"UTF-8\"?>";
    if (!output_func(userdata, decl, strlen(decl))) return false;
    if (cd_xml_sb_size(doc->nodes) != 0) {
        if (!cd_xml_write_element(doc, output_func, userdata, 0, 0, pretty)) return false;
    }
    if (!output_func(userdata, "\n", 1)) return false;
    return true;
}

#endif  // CD_XML_IMPLEMENTATION

#ifdef __cplusplus
}   // extern "C"
#endif

#endif  // CD_XML_H
