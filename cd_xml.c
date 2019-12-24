#include <stdio.h>
#include <assert.h>
#include "cd_xml.h"

//#ifndef CD_XML_LOG_ERROR
#define CD_XML_LOG_ERRORV(msg,...) fprintf(stderr, msg"\n", __VA_ARGS__)
#define CD_XML_LOG_ERROR(msg) fprintf(stderr, msg"\n")
#define CD_XML_LOG_DEBUGV(msg,...) fprintf(stderr, msg"\n", __VA_ARGS__)
#define CD_XML_LOG_DEBUG(msg) fprintf(stderr, msg"\n")
//#define CD_XML_LOG_ERROR(...) do ; while(0)
//#endif

typedef enum {
    CD_XML_TOKEN_EOF                    = 0,
    CD_XML_TOKEN_TAG_START              = '<',
    CD_XML_TOKEN_TAG_END                = '>',
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
    cd_xml_token_kind_t kind;
    const char* begin;
    const char* end;
} cd_xml_token_t;

typedef struct {
    cd_xml_doc_t* doc;
    const char* ptr;
    const char* end;
    
    cd_xml_token_t current;
    cd_xml_token_t matched;

    cd_xml_rv_t status;
} cd_xml_ctx_t;


static bool cd_xml_isspace(char c)
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

static bool cd_xml_is_name_char(char c)
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

static void cd_xml_next_token(cd_xml_ctx_t* ctx)
{
    ctx->matched = ctx->current;

restart:
    ctx->current.begin = ctx->ptr;
    ctx->current.end = ctx->ptr;
    if(ctx->end <= ctx->ptr || *ctx->ptr == '\0') {
        ctx->current.kind = CD_XML_TOKEN_EOF;
        return;
    }
    if((*ctx->ptr & 0x80) == 0) {
        // 7-bit ASCII

        ctx->current.kind = (cd_xml_token_kind_t)*ctx->ptr;
        switch (*ctx->ptr) {
        case ' ':           // skip space
        case '\t':
        case '\n':
        case '\r':
        case '\v':
        case '\f':
            do { ctx->ptr++; } while(ctx->ptr < ctx->end && cd_xml_isspace(ctx->ptr[0]));
            goto restart;
            break;
        case '/':
            ctx->ptr++;
            if(ctx->ptr < ctx->end && *ctx->ptr == '>') {
                ctx->ptr++;
                ctx->current.kind = CD_XML_TOKEN_EMPTYTAG_END;
            }
            ctx->current.end = ctx->ptr;
            break;
        case '<':
            ctx->current.kind = CD_XML_TOKEN_TAG_START;
            ctx->ptr++;
            if(ctx->ptr < ctx->end) {
                if(*ctx->ptr == '/') {
                    ctx->current.kind = CD_XML_TOKEN_ENDTAG_START;
                    ctx->ptr++;
                }
                else if(*ctx->ptr == '?') {
                    ctx->current.kind = CD_XML_TOKEN_PROC_INSTR_START;
                    ctx->ptr++;
                    if(ctx->ptr + 2 < ctx->end &&
                       ctx->ptr[0] == 'x' &&
                       ctx->ptr[1] == 'm' &&
                       ctx->ptr[2] == 'l') {
                        ctx->current.kind = CD_XML_TOKEN_XML_DECL_START;
                        ctx->ptr += 3;
                    }
                }
                else if(ctx->ptr + 2 < ctx->end &&
                        ctx->ptr[0] == '!' &&
                        ctx->ptr[1] == '-' &&
                        ctx->ptr[2] == '-') {   // xml comment
                    ctx->ptr += 3;
                    for(; ctx->ptr + 2 < ctx->end && *ctx->ptr != '\0'; ctx->ptr++) {
                        if(ctx->ptr[0] == '-' &&
                           ctx->ptr[1] == '-' &&
                           ctx->ptr[2] == '>') {
                            ctx->ptr += 3;
                            goto restart;
                        }
                    }
                    CD_XML_LOG_ERROR("EOF while scanning for end of comment");
                    ctx->current.kind = CD_XML_TOKEN_EOF;
                    ctx->status = CD_XML_PREMATURE_EOF;
                }
            }
            break;
        case '?':
            ctx->ptr++;
            if(ctx->ptr < ctx->end && *ctx->ptr == '>') {
                ctx->current.kind = CD_XML_TOKEN_PROC_INSTR_STOP;
                ctx->ptr++;
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
            ctx->current.kind = CD_XML_TOKEN_NAME;
            ctx->ptr++;
            do { ctx->ptr++; } while(ctx->ptr < ctx->end && cd_xml_is_name_char(ctx->ptr[0]));
            break;
        default:
            ctx->ptr++;
            break;
        }
        ctx->current.end = ctx->ptr;
    }
    else if ((*ctx->ptr & 0xe0) == 0xc0) {
        // 2-byte UTF-8
        if(ctx->end <= ctx->ptr + 1 || ctx->ptr[1] == '\0') {
            CD_XML_LOG_ERROR("2-byte UTF-8 span past end of data");
            ctx->current.kind = CD_XML_TOKEN_EOF;
            ctx->status = CD_XML_MALFORMED_UTF8;
            return;
        }
        else if((ctx->ptr[1] & 0xc0) != 0x80) {
            CD_XML_LOG_ERRORV("Illegal byte %u in 2-byte UTF-8 encoding", (unsigned)ctx->ptr[1]);
            ctx->current.kind = CD_XML_TOKEN_EOF;
            ctx->status = CD_XML_MALFORMED_UTF8;
            return;
        }
        ctx->ptr += 2;
        ctx->current.end = ctx->ptr;
        ctx->current.kind = CD_XML_TOKEN_UTF8;
    }
    else if ((*ctx->ptr & 0xf0) == 0xe0) {
        //  3-byte UTF-8
        if(ctx->end <= ctx->ptr + 2) {
            CD_XML_LOG_ERROR("3-byte UTF-8 span past end of data");
            ctx->current.kind = CD_XML_TOKEN_EOF;
            ctx->status = CD_XML_MALFORMED_UTF8;
            return;
        }
        for(size_t i=1; i<3; i++) {
            if((ctx->ptr[i] & 0xc0) != 0x80) {
                CD_XML_LOG_ERRORV("Byte %zu = %x illegal in 3-byte UTF-8 encoding", i, (unsigned)(ctx->ptr[1]));
                ctx->current.kind = CD_XML_TOKEN_EOF;
                ctx->status = CD_XML_MALFORMED_UTF8;
                return;
            }
        }
        ctx->ptr += 3;
        ctx->current.end = ctx->ptr;
        ctx->current.kind = CD_XML_TOKEN_UTF8;
    }
    else if ((*ctx->ptr & 0xf8) == 0xf0) {
        // 4-byte UTF-8
        if(ctx->end <= ctx->ptr + 3) {
            CD_XML_LOG_ERROR("3-byte UTF-8 span past end of data");
            ctx->current.kind = CD_XML_TOKEN_EOF;
            ctx->status = CD_XML_MALFORMED_UTF8;
            return;
        }
        for(size_t i=1; i<4; i++) {
            if((ctx->ptr[i] & 0xc0) != 0x80) {
                CD_XML_LOG_ERRORV("Byte %zu = %x illegal in 4-byte UTF-8 encoding", i, (unsigned)(ctx->ptr[1]));
                ctx->current.kind = CD_XML_TOKEN_EOF;
                ctx->status = CD_XML_MALFORMED_UTF8;
                return;
            }
        }
        ctx->ptr += 4;
        ctx->current.end = ctx->ptr;
        ctx->current.kind = CD_XML_TOKEN_UTF8;
    }
    else {
        CD_XML_LOG_ERRORV("Illegal UTF-8 start byte %u", (unsigned)*ctx->ptr);
        ctx->current.kind = CD_XML_TOKEN_EOF;
        ctx->status = CD_XML_MALFORMED_UTF8;
        return;
    }
}

static bool cd_xml_match_token(cd_xml_ctx_t* ctx, cd_xml_token_kind_t token_kind)
{
    if(ctx->current.kind == token_kind) {
        cd_xml_next_token(ctx);
        return true;
    }
    return false;
}

static void cd_xml_parse_xml_decl(cd_xml_ctx_t* ctx, bool is_decl)
{
    assert(ctx->matched.kind == is_decl ? CD_XML_TOKEN_XML_DECL_START : CD_XML_TOKEN_PROC_INSTR_START);
    const char* start = ctx->matched.begin;
    while(ctx->status == CD_XML_SUCCESS) {
        if(cd_xml_match_token(ctx, CD_XML_TOKEN_PROC_INSTR_STOP)) {
            int len = (int)(ctx->matched.end - start);
            CD_XML_LOG_DEBUGV("Skipped %sxml decl '%.*s'",
                              is_decl ? "xml decl" : "xml proc inst" ,
                              len, start);
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

cd_xml_rv_t cd_xml_parse(cd_xml_doc_t* doc, const char* data, size_t size)
{
    cd_xml_ctx_t ctx = {
        .doc = doc,
        .ptr = data,
        .end = data + size,
        .status = CD_XML_SUCCESS
    };
    
    cd_xml_next_token(&ctx);
    if(ctx.status != CD_XML_SUCCESS) return ctx.status;
    
    cd_xml_parse_prolog(&ctx);
    if(ctx.status != CD_XML_SUCCESS) return ctx.status;

    while(ctx.status == CD_XML_SUCCESS && ctx.current.kind != CD_XML_TOKEN_EOF) {
        const char * kind = "ascii";
        switch (ctx.current.kind) {
        case CD_XML_TOKEN_TAG_START:        kind = "TAG_START"; break;
        case CD_XML_TOKEN_TAG_END:          kind = "TAG_END"; break;
        case CD_XML_TOKEN_UTF8:             kind = "UTF-8"; break;
        case CD_XML_TOKEN_NAME:             kind = "TOKEN_NAME"; break;
        case CD_XML_TOKEN_EMPTYTAG_END:     kind = "EMPTYTAG_END"; break;
        case CD_XML_TOKEN_ENDTAG_START:     kind = "ENDTAG_START"; break;
        case CD_XML_TOKEN_PROC_INSTR_START: kind = "PROC_INSTR_START"; break;
        case CD_XML_TOKEN_PROC_INSTR_STOP:  kind = "PROC_INSTR_STOP"; break;
        case CD_XML_TOKEN_XML_DECL_START:   kind = "DECL_START"; break;
        default:
            break;
        }
        fprintf(stderr, "Token '%.*s' %s %zu bytes\n",
                (int)(ctx.current.end-ctx.current.begin), ctx.current.begin,
                kind,
                ctx.current.end-ctx.current.begin);
        cd_xml_next_token(&ctx);
    }
    return ctx.status;
}
