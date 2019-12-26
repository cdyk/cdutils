#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include "cd_xml.h"

//#ifndef CD_XML_LOG_ERROR
#define CD_XML_LOG_ERRORV(msg,...) fprintf(stderr, msg"\n", __VA_ARGS__)
#define CD_XML_LOG_ERROR(msg) fprintf(stderr, msg"\n")
#define CD_XML_LOG_DEBUGV(msg,...) fprintf(stderr, msg"\n", __VA_ARGS__)
#define CD_XML_LOG_DEBUG(msg) fprintf(stderr, msg"\n")
//#define CD_XML_LOG_ERROR(...) do ; while(0)
//#endif

#define CD_XML_MALLOC(size) malloc(size)
#define CD_XML_FREE(size) free(size)
#define CD_XML_REALLOC(ptr,size) realloc(ptr,size)

#define CD_XML_STRINGVIEW_FORMAT(text) (int)(text.end-text.begin),text.begin

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
    cd_xml_doc_t* doc;
    cd_xml_stringview_t input;
    cd_xml_chr_state_t chr;
    
    cd_xml_token_t current;
    cd_xml_token_t matched;

    cd_xml_buf_t* bufs;
    cd_xml_rv_t status;
} cd_xml_ctx_t;

static cd_xml_stringview_t cd_xml_stringview(const char* str)
{
    cd_xml_stringview_t rv = {
        .begin = str,
        .end = str
    };
    while(*rv.end != '\0') { rv.end++; }
    return rv;
}

static bool cd_xml_strcmp(cd_xml_stringview_t a, cd_xml_stringview_t b)
{
    if( (a.end - a.begin) != (b.end - b.begin)) return false;
    for(const char *p=a.begin, *q=b.begin; p<a.end; p++,q++) {
        if(*p != *q) return false;
    }
    return true;
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
            CD_XML_LOG_ERRORV("EOF inside %u-byte UTF-8 encoding", bytes);
            ctx->status = CD_XML_MALFORMED_UTF8;
            ctx->chr.code = 0;
            return;
        }
        unsigned code = (unsigned char)(*ctx->chr.text.end++);
        if((code & 0xc0) != 0x80) {
            CD_XML_LOG_ERRORV("Illegal byte %x inside %u-byte UTF-8 encoding", code, bytes);
            ctx->status = CD_XML_MALFORMED_UTF8;
            ctx->chr.code = 0;
            return;
        }
        ctx->chr.code = (ctx->chr.code << 6) | (code & 0x3f);
    }
}

static void cd_xml_next_char(cd_xml_ctx_t* ctx)
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
        CD_XML_LOG_ERRORV("Illegal UTF-8 start byte %u", (unsigned)*ctx->chr.text.end);
        ctx->chr.code = 0;
    }
}

static void cd_xml_next_token(cd_xml_ctx_t* ctx)
{
    if(ctx->status != CD_XML_SUCCESS) return;
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
                    CD_XML_LOG_ERROR("EOF while scanning for end of XML comment");
                    ctx->current.kind = CD_XML_TOKEN_EOF;
                    ctx->status = CD_XML_PREMATURE_EOF;
                    goto done;
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
        cd_xml_next_char(ctx);
        ctx->current.kind = CD_XML_TOKEN_NAME;
        do { cd_xml_next_char(ctx); } while(cd_xml_is_name_char(ctx->chr.code));
        break;
    default:
        cd_xml_next_char(ctx);
        break;
    }
done:
    ctx->current.text.end = ctx->chr.text.begin;
}

static bool cd_xml_match_token(cd_xml_ctx_t* ctx, cd_xml_token_kind_t token_kind)
{
    if(ctx->status == CD_XML_SUCCESS && ctx->current.kind == token_kind) {
        cd_xml_next_token(ctx);
        return true;
    }
    return false;
}

static bool cd_xml_expect_token(cd_xml_ctx_t* ctx, cd_xml_token_kind_t token_kind, const char* msg)
{
    if(ctx->status != CD_XML_SUCCESS) return false;
    if(ctx->current.kind == token_kind) {
        cd_xml_next_token(ctx);
        return ctx->status == CD_XML_SUCCESS;
    }
    ctx->status = CD_XML_STATUS_UNEXPECTED_TOKEN;
    CD_XML_LOG_ERRORV("%s, got %.*s", msg, CD_XML_STRINGVIEW_FORMAT(ctx->current.text));
    return false;
}

static cd_xml_stringview_t cd_xml_decode_entities(cd_xml_ctx_t* ctx, cd_xml_stringview_t in, unsigned amps)
{
    if(amps == 0) return in;
  
    ptrdiff_t size = in.end - in.begin;
    cd_xml_buf_t* buf = (cd_xml_buf_t*)CD_XML_MALLOC(sizeof(cd_xml_buf_t) + size);
    buf->next = ctx->bufs;
    ctx->bufs = buf;
    
    
    char* begin = buf->payload;
    char* end = begin;
    while(in.begin < in.end) {
        if(*in.begin == '&') {
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
                            CD_XML_LOG_ERRORV("Illegal hexidecimal digit %c in entity", c);
                            ctx->status = CD_XML_MALFORMED_ENTITY;
                            cd_xml_stringview_t rv = { NULL, NULL };
                            return rv;
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
                            CD_XML_LOG_ERRORV("Illegal decimal digit %c in entity", c);
                            ctx->status = CD_XML_MALFORMED_ENTITY;
                            cd_xml_stringview_t rv = { NULL, NULL };
                            return rv;
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
                    CD_XML_LOG_ERRORV("Entity code %x too large for UTF-8 encoding", code);
                    ctx->status = CD_XML_MALFORMED_ENTITY;
                    cd_xml_stringview_t rv = { NULL, NULL };
                    return rv;
                }
            }
            else {  // named entity
                cd_xml_stringview_t e = { .begin = in.begin };
                while(in.begin < in.end && *in.begin != ';') { in.begin++; }
                e.end = in.begin;

                if(cd_xml_strcmp(e, cd_xml_stringview("quot"))) {
                    assert(end - begin < size);
                    *end++ = '"';
                }
                else if(cd_xml_strcmp(e, cd_xml_stringview("amp"))) {
                    assert(end - begin < size);
                    *end++ = '&';
                }
                else if(cd_xml_strcmp(e, cd_xml_stringview("apos"))) {
                    assert(end - begin < size);
                    *end++ = '\'';
                }
                else if(cd_xml_strcmp(e, cd_xml_stringview("lt"))) {
                    assert(end - begin < size);
                    *end++ = '<';
                }
                else if(cd_xml_strcmp(e, cd_xml_stringview("gt"))) {
                    assert(end - begin < size);
                    *end++ = '>';
                }
                else {
                    CD_XML_LOG_ERRORV("Unrecognized named entity '%.*s'", CD_XML_STRINGVIEW_FORMAT(e));
                    ctx->status = CD_XML_MALFORMED_ENTITY;
                    cd_xml_stringview_t rv = { NULL, NULL };
                    return rv;
                }
            }
            
            if(*in.begin == *in.end || *in.begin != ';') {
                CD_XML_LOG_ERROR("Failed to find terminating ; of entity");
                ctx->status = CD_XML_MALFORMED_ENTITY;
                cd_xml_stringview_t rv = { NULL, NULL };
                return rv;
            }
            in.begin++;
        }
        else {
            assert(end - begin < size);
            *end++ = *in.begin++;
        }
    }
    
    cd_xml_stringview_t rv = { .begin = begin, .end = end };
    return rv;
}

static cd_xml_stringview_t cd_xml_parse_attribute_value(cd_xml_ctx_t* ctx)
{
    cd_xml_token_kind_t delimiter = ctx->current.kind;
    if((delimiter != CD_XML_TOKEN_QUOTE) && (delimiter != CD_XML_TOKEN_APOSTROPHE)) {
        CD_XML_LOG_ERROR("Expected attribute value enclosed by either ' or \"");
        ctx->status = CD_XML_STATUS_UNEXPECTED_TOKEN;
        return ctx->current.text;
    }
    unsigned amps = 0;
    cd_xml_stringview_t rv = ctx->chr.text;
    while(ctx->chr.code && (ctx->chr.code != delimiter)) {
        if(ctx->chr.code == '&') { amps++; }
        cd_xml_next_char(ctx);
    }
    if(ctx->chr.code == '\0') {
        CD_XML_LOG_ERROR("EOF while scanning for end of attribute value");
        ctx->status = CD_XML_PREMATURE_EOF;
        return ctx->current.text;
    }
    rv.end = ctx->chr.text.begin;
    cd_xml_next_char(ctx);
    cd_xml_next_token(ctx);
    return cd_xml_decode_entities(ctx, rv, amps);
}


static void cd_xml_parse_xml_decl(cd_xml_ctx_t* ctx, bool is_decl)
{
    assert(ctx->matched.kind == is_decl ? CD_XML_TOKEN_XML_DECL_START : CD_XML_TOKEN_PROC_INSTR_START);
    cd_xml_stringview_t match = { .begin = ctx->matched.text.begin };
    while(ctx->status == CD_XML_SUCCESS) {
        
        if(cd_xml_match_token(ctx, CD_XML_TOKEN_NAME)) {
            cd_xml_stringview_t name = ctx->matched.text;
            if(!cd_xml_expect_token(ctx, CD_XML_TOKEN_EQUAL, "Expected '='")) return;
            cd_xml_stringview_t value = cd_xml_parse_attribute_value(ctx);

            if(is_decl) {

                if(cd_xml_strcmp(name, cd_xml_stringview("version"))) {
                    if(cd_xml_strcmp(value, cd_xml_stringview("1.0"))) { }
                    else {
                        CD_XML_LOG_ERRORV("Unsupported xml version %.*s", CD_XML_STRINGVIEW_FORMAT(value));
                        ctx->status = CD_XML_STATUS_UNSUPPORTED_VERSION;
                        return;
                    }
                }
                else if(cd_xml_strcmp(name, cd_xml_stringview("encoding"))) {
                    if(cd_xml_strcmp(value, cd_xml_stringview("ASCII"))) { }
                    if(cd_xml_strcmp(value, cd_xml_stringview("UTF-8"))) { }
                    else {
                        CD_XML_LOG_ERRORV("Unsupported encoding %.*s", CD_XML_STRINGVIEW_FORMAT(value));
                        ctx->status = CD_XML_STATUS_UNSUPPORTED_ENCODING;
                        return;
                    }
                }
                else if(cd_xml_strcmp(name, cd_xml_stringview("standalone"))) { /* ignore for now */ }
                else {
                    CD_XML_LOG_ERRORV("Unrecognized declaration attribute '%.*s'='%.*s'",
                                      CD_XML_STRINGVIEW_FORMAT(name),
                                      CD_XML_STRINGVIEW_FORMAT(value));
                }
            }

            CD_XML_LOG_DEBUGV("zAttribute '%.*s'='%.*s'",
                              CD_XML_STRINGVIEW_FORMAT(name),
                              CD_XML_STRINGVIEW_FORMAT(value));
        }
        else if(cd_xml_match_token(ctx, CD_XML_TOKEN_PROC_INSTR_STOP)) {
            match.end = ctx->matched.text.end;
            CD_XML_LOG_DEBUGV("Skipped %s '%.*s'",
                              is_decl ? "xml decl" : "xml proc inst",
                              CD_XML_STRINGVIEW_FORMAT(match));
            return;
        }
        else if(cd_xml_match_token(ctx, CD_XML_TOKEN_EOF)) {
            CD_XML_LOG_ERRORV("EOF while parsing %s",
                              is_decl ? "xml decl" : "xml proc inst");
            ctx->status = CD_XML_PREMATURE_EOF;
            return;
        }
        else {
            cd_xml_next_token(ctx);
        }
    }
}

static void cd_xml_parse_prolog(cd_xml_ctx_t* ctx)
{
    if(cd_xml_match_token(ctx, CD_XML_TOKEN_XML_DECL_START)) {
        cd_xml_parse_xml_decl(ctx, true);
    }
    while(cd_xml_match_token(ctx, CD_XML_TOKEN_PROC_INSTR_START)) {
        cd_xml_parse_xml_decl(ctx, false);
    }
}

static cd_xml_attribute_t* cd_xml_parse_attribute(cd_xml_ctx_t* ctx)
{
    assert(ctx->matched.kind == CD_XML_TOKEN_NAME);

    cd_xml_stringview_t ns = { NULL, NULL };
    cd_xml_stringview_t name = ctx->matched.text;
    if(cd_xml_match_token(ctx, CD_XML_TOKEN_COLON)) {
        ns = name;
        name = ctx->matched.text;
    }

    if(!cd_xml_expect_token(ctx, CD_XML_TOKEN_EQUAL, "Expected '=' after attribute name")) return NULL;

    cd_xml_stringview_t value = cd_xml_parse_attribute_value(ctx);
    if(ctx->status != CD_XML_SUCCESS) return NULL;

    CD_XML_LOG_DEBUGV("Attribute %.*s:%.*s='%.*s'",
                      CD_XML_STRINGVIEW_FORMAT(ns),
                      CD_XML_STRINGVIEW_FORMAT(name),
                      CD_XML_STRINGVIEW_FORMAT(value));

#if 0
    if(cd_xml_cmp_stringview_str(&ns, "xmlns")) {
        CD_XML_LOG_DEBUGV("New namespace %.*s=%.*s",
                          CD_XML_STRINGVIEW_FORMAT(name),
                          CD_XML_STRINGVIEW_FORMAT(value));
    }
    else {
        CD_XML_LOG_DEBUGV("Attribute %.*s=%.*s",
                          CD_XML_STRINGVIEW_FORMAT(name),
                          CD_XML_STRINGVIEW_FORMAT(value));
    }
#endif
    return NULL;
}

static cd_xml_element_t* cd_xml_parse_element(cd_xml_ctx_t* ctx)
{
    assert(ctx->matched.kind == CD_XML_TOKEN_TAG_START);
    if(!cd_xml_expect_token(ctx, CD_XML_TOKEN_NAME, "Expected element name")) return NULL;

    cd_xml_stringview_t ns = { NULL, NULL };
    cd_xml_stringview_t name = ctx->matched.text;
    if(cd_xml_match_token(ctx, CD_XML_TOKEN_COLON)) {
        ns = name;
        name = ctx->matched.text;
    }

    while(cd_xml_match_token(ctx, CD_XML_TOKEN_NAME)) {
        cd_xml_attribute_t* attribute = cd_xml_parse_attribute(ctx);
    }

    if(cd_xml_match_token(ctx, CD_XML_TOKEN_EMPTYTAG_END)) {
        CD_XML_LOG_DEBUGV("Element %.*s:%.*s",
                          CD_XML_STRINGVIEW_FORMAT(ns),
                          CD_XML_STRINGVIEW_FORMAT(name));
        return NULL;
    }
    else if(cd_xml_match_token(ctx, CD_XML_TOKEN_TAG_END)) {
        unsigned amps = 0;
        cd_xml_stringview_t text = { NULL, NULL};
        while(ctx->status == CD_XML_SUCCESS) {
            if(cd_xml_match_token(ctx, CD_XML_TOKEN_ENDTAG_START)) {
                if(!cd_xml_expect_token(ctx, CD_XML_TOKEN_NAME, "In end-tag, expected name")) return NULL;
                if(!cd_xml_expect_token(ctx, CD_XML_TOKEN_TAG_END, "In end-tag, expected >")) return NULL;
                if(text.begin != NULL) {
                    text = cd_xml_decode_entities(ctx, text, amps);
                    CD_XML_LOG_DEBUGV("Text '%.*s'", CD_XML_STRINGVIEW_FORMAT(text));
                    text.begin = NULL;
                }
                break;
            }
            else if(cd_xml_match_token(ctx, CD_XML_TOKEN_TAG_START)) {
                if(text.begin != NULL) {
                    text = cd_xml_decode_entities(ctx, text, amps);
                    CD_XML_LOG_DEBUGV("Text '%.*s'", CD_XML_STRINGVIEW_FORMAT(text));
                    text.begin = NULL;
                }
                cd_xml_element_t* child = cd_xml_parse_element(ctx);
                if(ctx->status != CD_XML_SUCCESS) return NULL;
            }
            else if(ctx->current.kind == CD_XML_TOKEN_EOF) {
                CD_XML_LOG_ERRORV("EOF while scanning for end of tag %.*s:%.*s",
                                  CD_XML_STRINGVIEW_FORMAT(ns),
                                  CD_XML_STRINGVIEW_FORMAT(name));
                ctx->status = CD_XML_PREMATURE_EOF;
                return NULL;
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
        
        CD_XML_LOG_DEBUGV("Element %.*s:%.*s",
                          CD_XML_STRINGVIEW_FORMAT(ns),
                          CD_XML_STRINGVIEW_FORMAT(name));
        return NULL;
    }
    else {
        CD_XML_LOG_ERRORV("Expected either attribute name > or />, got '%.*s'", CD_XML_STRINGVIEW_FORMAT(ctx->current.text));
        ctx->status = CD_XML_STATUS_UNEXPECTED_TOKEN;
        return NULL;
    }
}

cd_xml_rv_t cd_xml_parse(cd_xml_doc_t* doc, const char* data, size_t size)
{
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
        .bufs = NULL,
        .status = CD_XML_SUCCESS
    };
    
    cd_xml_next_char(&ctx);
    cd_xml_next_token(&ctx);
    if(ctx.status != CD_XML_SUCCESS) return ctx.status;
    
    cd_xml_parse_prolog(&ctx);
    if(ctx.status != CD_XML_SUCCESS) return ctx.status;

    if(cd_xml_expect_token(&ctx, CD_XML_TOKEN_TAG_START, "Expected element start '<'")) {
        cd_xml_parse_element(&ctx);
    }
    cd_xml_expect_token(&ctx, CD_XML_TOKEN_EOF, "Expexted EOF");
    return ctx.status;
}
