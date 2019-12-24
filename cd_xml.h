#ifndef CD_XML_H
#define CD_XML_H

#include <stdint.h>
#include <stdbool.h>

typedef struct {
    
} cd_xml_doc_t;

typedef enum
{
    CD_XML_SUCCESS = 0,
    CD_XML_MALFORMED_UTF8,
    CD_XML_PREMATURE_EOF
} cd_xml_rv_t;


cd_xml_rv_t cd_xml_parse(cd_xml_doc_t* doc, const char* data, size_t size);

#endif // CD_XML_H
