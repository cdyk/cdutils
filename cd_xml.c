#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <stdarg.h>
#include "cd_xml.h"

#define CD_XML_MALLOC(size) malloc(size)
#define CD_XML_FREE(size) free(size)
#define CD_XML_REALLOC(ptr,size) realloc(ptr,size)

#define CD_XML_STRINGVIEW_FORMAT(text) (int)((text).end-(text).begin),(text).begin

typedef enum {
    CD_XML_TOKEN_EOF                    = 0,
    CD_XML_TOKEN_QUOTE                  = '"',  // 34
    CD_XML_TOKEN_AMP                    = '&',  // 38
    CD_XML_TOKEN_APOSTROPHE             = '\'', // 39
    CD_XML_TOKEN_COLON                  = ':',  // 58
    CD_XML_TOKEN_TAG_START              = '<',  // 60
    CD_XML_TOKEN_EQUAL                  = '=',  // 61
    CD_XML_TOKEN_TAG_END                = '>',  // 62
    CD_XML_TOKEN_LASTCHAR               = 127,
    CD_XML_TOKEN_UTF8,
    CD_XML_TOKEN_NAME,
    CD_XML_TOKEN_EMPTYTAG_END,          // />
    CD_XML_TOKEN_ENDTAG_START,          // </
    CD_XML_TOKEN_PROC_INSTR_START,      // <?
    CD_XML_TOKEN_PROC_INSTR_STOP,       // ?>
    CD_XML_TOKEN_XML_DECL_START         // <?xml
} cd_xml_token_kind_t;

typedef struct {
    cd_xml_stringview_t text;
    cd_xml_token_kind_t kind;
} cd_xml_token_t;

typedef struct cd_xml_buf_struct {
    struct cd_xml_buf_struct* next;
    char payload[0];
} cd_xml_buf_t;

typedef struct {
    cd_xml_stringview_t text;
    uint32_t code;
} cd_xml_chr_state_t;

typedef struct {
    cd_xml_stringview_t ns;
    cd_xml_stringview_t name;
    cd_xml_stringview_t value;
} cd_xml_att_triple_t;

typedef struct {
    cd_xml_stringview_t prefix;
    cd_xml_ns_ix_t ns;
} cd_xml_ns_bind_t;

typedef struct {
    cd_xml_doc_t* doc;
    cd_xml_stringview_t input;
    cd_xml_chr_state_t chr;
    
    cd_xml_token_t current;
    cd_xml_token_t matched;

    cd_xml_att_triple_t* tmp_atts;
    
    cd_xml_ns_ix_t ns_default;
    cd_xml_ns_bind_t* ns_bind_stack;
    
    cd_xml_buf_t* bufs;
    unsigned depth;

    cd_xml_status_t status;
} cd_xml_ctx_t;

#define CD_XML_MIN(a,b) ((a)<(b)?(a):(b))
#define CD_XML_MAX(a,b) ((a)<(b)?(b):(a))

#define cd_xml_strv_empty(a) (a.begin == a.end)

// Stretchy buf ala  https://github.com/nothings/stb/blob/master/stretchy_buffer.h

#define cd_xml__sb_base(a) ((unsigned*)(a)-2)
#define cd_xml__sb_size(a) (cd_xml__sb_base(a)[0])
#define cd_xml__sb_cap(a) (cd_xml__sb_base(a)[1])
#define cd_xml__sb_must_grow(a) (((a)==NULL)||(cd_xml__sb_cap(a) <= cd_xml__sb_size(a)+1u))
#define cd_xml__sb_do_grow(a) (*(void**)&(a)=cd_xml__sb_grow((a),sizeof(*(a))))
#define cd_xml__sb_maybe_grow(a) (cd_xml__sb_must_grow(a)?cd_xml__sb_do_grow(a):0)

#define cd_xml_sb_free(a) ((a)?CD_XML_FREE(cd_xml__sb_base(a):0)
#define cd_xml_sb_size(a) ((a)?cd_xml__sb_size(a):0u)
#define cd_xml_sb_push(a,x) (cd_xml__sb_maybe_grow(a),(a)[cd_xml__sb_size(a)++]=(x))

#define cd_xml_sb_shrink(a,n) ((a)&&((n)<cd_xml__sb_size(a))?cd_xml__sb_size(a)=(n):0)

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

static void cd_xml_report_error(cd_xml_ctx_t* ctx, const char* a, const char* b, const char* fmt, ...)
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

static void cd_xml_report_debug(cd_xml_ctx_t* ctx, const char* fmt, ...)
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

static void cd_xml_consume_utf8(cd_xml_ctx_t* ctx, unsigned bytes)
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

static bool cd_xml_next_char(cd_xml_ctx_t* ctx)
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

static bool cd_xml_next_token(cd_xml_ctx_t* ctx)
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

static bool cd_xml_match_token(cd_xml_ctx_t* ctx, cd_xml_token_kind_t token_kind)
{
    if(ctx->status == CD_XML_STATUS_SUCCESS && ctx->current.kind == token_kind) {
        if (cd_xml_next_token(ctx)) {
            return true;
        }
    }
    return false;
}

static bool cd_xml_expect_token(cd_xml_ctx_t* ctx, cd_xml_token_kind_t token_kind, const char* msg)
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

static bool cd_xml_decode_entities(cd_xml_ctx_t* ctx, cd_xml_stringview_t* out, cd_xml_stringview_t in, unsigned amps)
{
    if (amps == 0) {
        *out = in;
        return true;
    }
  
    ptrdiff_t size = in.end - in.begin;
    cd_xml_buf_t* buf = (cd_xml_buf_t*)CD_XML_MALLOC(sizeof(cd_xml_buf_t) + size);
    buf->next = ctx->bufs;
    ctx->bufs = buf;
 
    char* begin = buf->payload;
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

static bool cd_xml_parse_attribute_value(cd_xml_ctx_t* ctx, cd_xml_stringview_t* out)
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

static bool cd_xml_parse_xml_decl(cd_xml_ctx_t* ctx, bool is_decl)
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

            cd_xml_report_debug(ctx, "Attribute '%.*s'='%.*s'",
                                CD_XML_STRINGVIEW_FORMAT(name),
                                CD_XML_STRINGVIEW_FORMAT(value));
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

static bool cd_xml_parse_prolog(cd_xml_ctx_t* ctx)
{
    if(cd_xml_match_token(ctx, CD_XML_TOKEN_XML_DECL_START)) {
        if (!cd_xml_parse_xml_decl(ctx, true)) return false;
    }
    while(cd_xml_match_token(ctx, CD_XML_TOKEN_PROC_INSTR_START)) {
        if (!cd_xml_parse_xml_decl(ctx, false)) return false;
    }
    return true;
}

static bool cd_xml_parse_attribute(cd_xml_ctx_t* ctx)
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
        return ns != cd_xml_no_ix;
    }

    // Default namespace
    if(ns.begin == NULL && cd_xml_strcmp(&name, "xmlns")) {
        if(cd_xml_strv_empty(value)) {
            ctx->status = CD_XML_STATUS_MALFORMED_ATTRIBUTE;
            cd_xml_report_error(ctx, name.begin, ctx->chr.text.end, "Empty namespace uri");
            return false;
        }

        cd_xml_ns_ix_t ns = cd_xml_add_namespace(ctx->doc, NULL, &value);
        return ns != cd_xml_no_ix;
    }
    
    cd_xml_att_triple_t att = {
        .ns = ns,
        .name = name,
        .value = value
    };
    cd_xml_sb_push(ctx->tmp_atts, att);
    return true;
}

static bool cd_xml_parse_element_tag_start(cd_xml_ctx_t* ctx, cd_xml_stringview_t* ns, cd_xml_stringview_t* name)
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

static bool cd_xml_parse_element(cd_xml_ctx_t* ctx, cd_xml_node_ix_t parent);

static bool cd_xml_parse_element_contents(cd_xml_ctx_t* ctx, cd_xml_stringview_t* ns, cd_xml_stringview_t* name, cd_xml_node_ix_t parent)
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

            ctx->depth++;
            if(!cd_xml_parse_element(ctx, parent)) return false;
            ctx->depth--;

            if(ctx->status != CD_XML_STATUS_SUCCESS) return false;
        }

        else if(ctx->current.kind == CD_XML_TOKEN_EOF) {
            ctx->status = CD_XML_STATUS_PREMATURE_EOF;
            cd_xml_report_error(ctx, tag_start, ctx->chr.text.end,
                                "EOF while scanning for end of tag %.*s:%.*s",
                                CD_XML_STRINGVIEW_FORMAT(*ns),
                                CD_XML_STRINGVIEW_FORMAT(*name));
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

static bool cd_xml_parse_element(cd_xml_ctx_t* ctx, cd_xml_node_ix_t parent)
{
    cd_xml_ns_ix_t parent_default_ns = ctx->ns_default;
    unsigned parent_bind_stack_height = cd_xml_sb_size(ctx->ns_bind_stack);
    cd_xml_sb_shrink(ctx->tmp_atts, 0);

    cd_xml_stringview_t ns = { NULL, NULL };
    cd_xml_stringview_t name = ctx->matched.text;

    if(cd_xml_parse_element_tag_start(ctx, &ns, &name)) {

        for(unsigned i=0; i<cd_xml_sb_size(ctx->tmp_atts); i++) {
            cd_xml_att_triple_t* att = &ctx->tmp_atts[i];
            cd_xml_report_debug(ctx, "tmp att %u %.*s:%.*s=%.*s", i,
                                CD_XML_STRINGVIEW_FORMAT(att->ns),
                                CD_XML_STRINGVIEW_FORMAT(att->name),
                                CD_XML_STRINGVIEW_FORMAT(att->value));
        }

        cd_xml_ns_ix_t ns_ix = cd_xml_no_ix;    // FIXME

        cd_xml_node_ix_t elem_ix = cd_xml_add_element(ctx->doc, ns_ix, &name, parent);

        if(cd_xml_parse_element_contents(ctx, &ns, &name, elem_ix)) {
            cd_xml_sb_shrink(ctx->ns_bind_stack, parent_bind_stack_height);
            ctx->ns_default = parent_default_ns;
            return true;
        }
    }
    cd_xml_sb_shrink(ctx->ns_bind_stack, parent_bind_stack_height);
    ctx->ns_default = parent_default_ns;
    return false;
}

cd_xml_att_ix_t cd_xml_add_namespace(cd_xml_doc_t* doc,
                                     cd_xml_stringview_t* prefix,
                                     cd_xml_stringview_t* uri)
{
    assert(uri->begin < uri->end && "URI cannot be empty");

    // Assume that number of namespaces are quite low, so
    // a linear search will do for now.
    for(unsigned i=0; i<cd_xml_sb_size(doc->ns); i++) {
        if(cd_xml_strvcmp(&doc->ns[i].uri, uri)) {
            // Match
            return i;
        }
    }
    // Register new namespace
    unsigned ix = cd_xml_sb_size(doc->ns);
    cd_xml_ns_t x = {0};
    if (prefix) {
        x.prefix = *prefix;
    }
    x.uri = *uri;
    cd_xml_sb_push(doc->ns, x);
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
        .text = *content,
        .kind = CD_XML_NODE_TEXT
    };
    cd_xml_node_ix_t elem_ix = cd_xml_sb_size(doc->nodes);
    cd_xml_sb_push(doc->nodes, element);
    if (parent != cd_xml_no_ix) {
        if (doc->nodes[parent].first_child == cd_xml_no_ix) {    // first child of parent
            doc->nodes[parent].first_child = elem_ix;
            doc->nodes[parent].last_child = elem_ix;
        }
        else {
            doc->nodes[doc->nodes[parent].last_child].next_sibling = elem_ix;
            doc->nodes[parent].last_child = elem_ix;
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
        .first_child = cd_xml_no_ix,
        .last_child = cd_xml_no_ix,
        .next_sibling = cd_xml_no_ix,
        .name = *name,
        .ns = ns,
        .first_attribute = 0,
        .kind = CD_XML_NODE_ELEMENT
    };
    cd_xml_node_ix_t elem_ix = cd_xml_sb_size(doc->nodes);
    cd_xml_sb_push(doc->nodes, element);
    if (parent != cd_xml_no_ix) {
        if (doc->nodes[parent].first_child == cd_xml_no_ix) {    // first child of parent
            doc->nodes[parent].first_child = elem_ix;
            doc->nodes[parent].last_child = elem_ix;
        }
        else {
            doc->nodes[doc->nodes[parent].last_child].next_sibling = elem_ix;
            doc->nodes[parent].last_child = elem_ix;
        }
    }
    return elem_ix;
}


void cd_xml_init(cd_xml_doc_t* doc)
{
    if (doc == NULL) return;

    *doc = (cd_xml_doc_t){ 0 };
}

void cd_xml_free(cd_xml_doc_t* doc)
{
    if (doc == NULL) return;

}

cd_xml_status_t cd_xml_init_and_parse(cd_xml_doc_t* doc, const char* data, size_t size)
{
    cd_xml_init(doc);
    cd_xml_ctx_t ctx = {
        .doc = doc,
        .input = {
            .begin = data,
            .end = data + size
        },
        .chr = {
            .text = {
                .end = data
            }
        },
        .ns_default = cd_xml_no_ix,
        .bufs = NULL,
        .depth = 0,
        
        .status = CD_XML_STATUS_SUCCESS
    };
    
    if (cd_xml_next_char(&ctx) && cd_xml_next_token(&ctx)) {

    }

    if (ctx.status != CD_XML_STATUS_SUCCESS) goto fail;
    
    cd_xml_parse_prolog(&ctx);
    if (ctx.status != CD_XML_STATUS_SUCCESS) goto fail;

    if(cd_xml_expect_token(&ctx, CD_XML_TOKEN_TAG_START, "Expected element start '<'")) {
        cd_xml_parse_element(&ctx, cd_xml_no_ix);
    }
    cd_xml_expect_token(&ctx, CD_XML_TOKEN_EOF, "Expexted EOF");

    return ctx.status;

fail:
    cd_xml_free(doc);
    return ctx.status;
}

#define CD_XML_WRITE_HELPER(a) (a), strlen(a)
#define CD_XML_WRITE_HELPERV(a) (a).begin, (a).end-(a).begin

static bool cd_xml_encode_and_write(cd_xml_output_func output_func, void* userdata, cd_xml_stringview_t* text)
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

static bool cd_xml_write_element(cd_xml_doc_t* doc, cd_xml_output_func output_func, void* userdata, cd_xml_node_ix_t elem_ix, unsigned depth, bool pretty)
{
    static const char* indent = "\n                                        ";
    const size_t indent_l = strlen(indent);

    assert(elem_ix < cd_xml_sb_size(doc->nodes));
    cd_xml_node_t* elem = &doc->nodes[elem_ix];

    if (pretty) {
        if (!output_func(userdata, indent, CD_XML_MIN(2 * (size_t)depth + 1, indent_l))) return false;
    }
    if (elem->kind == CD_XML_NODE_ELEMENT) {
        if (!output_func(userdata, "<", 1)) return false;
        if (!output_func(userdata, elem->name.begin, elem->name.end - elem->name.begin)) return false;

        if (elem_ix == 0) {


            for (unsigned i = 0; i < cd_xml_sb_size(doc->ns); i++) {
                cd_xml_ns_t* ns = &doc->ns[i];

                if (i == 0 || !pretty) {
                    if (!output_func(userdata, " ", 1)) return false;
                }
                else {
                    if (!output_func(userdata, indent, CD_XML_MIN(2 * (size_t)depth + 3 + (elem->name.end - elem->name.begin), indent_l))) return false;
                }

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

/*            
                fprintf(stderr, "NS %u: prefix='%.*s', uri='%.*s'\n", i,
                        CD_XML_STRINGVIEW_FORMAT(doc->ns[i].prefix),
                        CD_XML_STRINGVIEW_FORMAT(doc->ns[i].uri));
            }
            */

        }

        if (elem->first_child == cd_xml_no_ix) {
            if (!output_func(userdata, "/>", 2)) return false;
            return true;
        }
        else {
            if (!output_func(userdata, ">", 1)) return false;

            for (cd_xml_node_ix_t child_ix = elem->first_child; child_ix != cd_xml_no_ix; child_ix = doc->nodes[child_ix].next_sibling) {
                if (!cd_xml_write_element(doc, output_func, userdata, child_ix, depth + 1, pretty)) return false;
            }
            if (pretty) {
                if (!output_func(userdata, indent, CD_XML_MIN(2 * depth + 1, indent_l))) return false;
            }
            if (!output_func(userdata, "<//", 2)) return false;
            if (!output_func(userdata, elem->name.begin, elem->name.end - elem->name.begin)) return false;
            if (!output_func(userdata, ">", 1)) return false;
        }
    }
    else if (elem->kind == CD_XML_NODE_TEXT) {
        if (!cd_xml_encode_and_write(output_func, userdata, &elem->text)) return false;
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
