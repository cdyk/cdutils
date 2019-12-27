#ifndef CD_XML_H
#define CD_XML_H

#include <stdint.h>
#include <stdbool.h>

typedef struct {
    const char* begin;
    const char* end;
} cd_xml_stringview_t;

typedef struct {
  char dummy;
} cd_xml_attribute_t;

typedef struct {
  char dummy;
} cd_xml_element_t;

typedef struct {
  char dummy;
} cd_xml_doc_t;

typedef enum
{
    CD_XML_STATUS_SUCCESS = 0,
    CD_XML_STATUS_UNSUPPORTED_VERSION,
    CD_XML_STATUS_UNSUPPORTED_ENCODING,
    CD_XML_STATUS_MALFORMED_UTF8,
    CD_XML_STATUS_PREMATURE_EOF,
    CD_XML_STATUS_MALFORMED_DECLARATION,
    CD_XML_STATUS_UNEXPECTED_TOKEN,
    CD_XML_STATUS_MALFORMED_ENTITY
} cd_xml_status_t;


void cd_xml_init(cd_xml_doc_t* doc);
void cd_xml_release(cd_xml_doc_t* doc);
cd_xml_status_t cd_xml_init_and_parse(cd_xml_doc_t* doc, const char* data, size_t size);

#endif // CD_XML_H
