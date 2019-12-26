#ifndef CD_XML_H
#define CD_XML_H

#include <stdint.h>
#include <stdbool.h>

typedef struct {
    const char* begin;
    const char* end;
} cd_xml_stringview_t;

typedef struct {
    
} cd_xml_attribute_t;

typedef struct {
    
} cd_xml_element_t;

typedef struct {
    
} cd_xml_doc_t;

typedef enum
{
    CD_XML_SUCCESS = 0,
    CD_XML_STATUS_UNSUPPORTED_VERSION,
    CD_XML_STATUS_UNSUPPORTED_ENCODING,
    CD_XML_MALFORMED_UTF8,
    CD_XML_PREMATURE_EOF,
    CD_XML_STATUS_UNEXPECTED_TOKEN,
    CD_XML_MALFORMED_ENTITY
} cd_xml_rv_t;


cd_xml_rv_t cd_xml_parse(cd_xml_doc_t* doc, const char* data, size_t size);

#endif // CD_XML_H
