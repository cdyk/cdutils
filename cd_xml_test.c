#include <stdio.h>

#include "cd_xml.h"




#include <stdio.h>
#include <assert.h>
#include <string.h>

int main(int argc, const char * argv[]) {
    
    {
        const char* xml =
            "<?xml version=\"1.0\" encoding=\"UTF-8\"?>"
            "<foo>ᚠᚢᚦᚨᚱᚲ € æøå 𠜎</foo>";
        cd_xml_doc_t doc;
        cd_xml_rv_t rv = cd_xml_parse(&doc, xml, strlen(xml));
        assert(rv == CD_XML_SUCCESS);
        
    }
    
    
    // insert code here...
    printf("Hello, World!\n");
    return 0;
}
