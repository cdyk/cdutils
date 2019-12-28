#ifndef CD_XML_H
#define CD_XML_H

#include <stdint.h>
#include <stdbool.h>

typedef uint32_t cd_xml_ns_ix_t;
typedef uint32_t cd_xml_att_ix_t;
typedef uint32_t cd_xml_node_ix_t;
static const uint32_t cd_xml_no_ix = (uint32_t)-1;

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

typedef struct {
    union {
        struct {
            cd_xml_stringview_t name;
            cd_xml_ns_ix_t ns;
            cd_xml_node_ix_t first_child;
            cd_xml_node_ix_t last_child;
            cd_xml_att_ix_t first_attribute;
        };
        struct {
            cd_xml_stringview_t text;
        };
    };
    cd_xml_node_ix_t next_sibling;
    cd_xml_node_kind_t kind;
} cd_xml_node_t;

typedef struct cd_xml_ns_struct {
    cd_xml_stringview_t prefix;
    cd_xml_stringview_t uri;
} cd_xml_ns_t;

typedef struct {
    cd_xml_ns_t* ns;                // Stretchy buf
    cd_xml_node_t* nodes;           // Stretchy buf
    cd_xml_attribute_t* attributes; // Stretchy buf
} cd_xml_doc_t;

typedef enum
{
    CD_XML_STATUS_SUCCESS = 0,
    CD_XML_STATUS_UNSUPPORTED_VERSION,
    CD_XML_STATUS_UNSUPPORTED_ENCODING,
    CD_XML_STATUS_MALFORMED_UTF8,
    CD_XML_STATUS_MALFORMED_ATTRIBUTE,
    CD_XML_STATUS_PREMATURE_EOF,
    CD_XML_STATUS_MALFORMED_DECLARATION,
    CD_XML_STATUS_UNEXPECTED_TOKEN,
    CD_XML_STATUS_MALFORMED_ENTITY
} cd_xml_status_t;


void cd_xml_init(cd_xml_doc_t* doc);

void cd_xml_free(cd_xml_doc_t* doc);

cd_xml_att_ix_t cd_xml_add_namespace(cd_xml_doc_t* doc, cd_xml_stringview_t* prefix, cd_xml_stringview_t* uri);

cd_xml_node_ix_t cd_xml_add_element(cd_xml_doc_t* doc, cd_xml_ns_ix_t ns, cd_xml_stringview_t* name, cd_xml_node_ix_t parent);

cd_xml_node_ix_t cd_xml_add_text(cd_xml_doc_t* doc, cd_xml_stringview_t* content, cd_xml_node_ix_t parent);

cd_xml_status_t cd_xml_init_and_parse(cd_xml_doc_t* doc, const char* data, size_t size);

typedef bool (*cd_xml_output_func)(void* userdata, const char* ptr, size_t bytes);

bool cd_xml_write(cd_xml_doc_t* doc, cd_xml_output_func output_func, void* userdata, bool pretty);

#endif // CD_XML_H
