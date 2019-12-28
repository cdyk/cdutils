#include "cd_xml.h"

#include <cstdio>
#include <cassert>
#include <cstring>

namespace {

    bool output_func(void* userdata, const char* ptr, size_t bytes)
    {
        fprintf(stderr, "%.*s", (int)bytes, ptr);
        return true;
    }

}

int main(int argc, const char * argv[]) {
    {   // Namespaces
        const char* xml =
            "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
            "<b:quux />\n";
        cd_xml_doc_t* doc = NULL;
        auto rv = cd_xml_init_and_parse(&doc, xml, std::strlen(xml));
        assert(rv == CD_XML_STATUS_SUCCESS);
        cd_xml_write(doc, output_func, nullptr, true);
        cd_xml_free(&doc);
    }
    {   // Misc UTF-tests
        const char* xml =
            "<?xml version=\"1.0\" encoding=\"UTF-8\"?><!--m oo-->"
            "<foo moo=' doo  '><gah quux='waldo&lt;&#x5d0;'>   ᚠᚢᚦ&amp;ᚨᚱᚲ €  <meep/> æøå 𠜎  </gah><meh>meh</meh></foo>";
        cd_xml_doc_t* doc = NULL;
        auto rv = cd_xml_init_and_parse(&doc, xml, std::strlen(xml));
        assert(rv == CD_XML_STATUS_SUCCESS);
        cd_xml_write(doc, output_func, NULL, true);
        cd_xml_free(&doc);
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
        cd_xml_doc_t* doc = NULL;
        cd_xml_parse_status_t rv = cd_xml_init_and_parse(&doc, xml, strlen(xml));
        assert(rv == CD_XML_STATUS_SUCCESS);
        cd_xml_write(doc, output_func, NULL, true);
        cd_xml_free(&doc);
    }

    return 0;
}
