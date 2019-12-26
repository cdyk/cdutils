#include <stdio.h>

#include "cd_xml.h"




#include <stdio.h>
#include <assert.h>
#include <string.h>

int main(int argc, const char * argv[]) {
    
    {   // Misc UTF-tests
        const char* xml =
            "<?xml version=\"1.0\" encoding=\"UTF-8\"?><!--m oo-->"
            "<foo moo=' doo  '><gah quux='waldo&lt;&#x5d0;'>   ᚠᚢᚦ&amp;ᚨᚱᚲ €  <meep/> æøå 𠜎  </gah><meh>meh</meh></foo>";
        cd_xml_doc_t doc;
        cd_xml_rv_t rv = cd_xml_parse(&doc, xml, strlen(xml));
        assert(rv == CD_XML_SUCCESS);
    }
    {   // Namespaces
        const char* xml =
            "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
            "<foo xmlns='http://a.com'>\n"
            "  <bar xmlns:b='http://b.com'>\n"
            "    <baz xmlns:c='http://c.com'>\n"
            "     <b:quux/>\n"
            "    </baz>\n"
            "  </bar>\n"
            "</foo>\n";
        cd_xml_doc_t doc;
        cd_xml_rv_t rv = cd_xml_parse(&doc, xml, strlen(xml));
        assert(rv == CD_XML_SUCCESS);
    }

    return 0;
}
