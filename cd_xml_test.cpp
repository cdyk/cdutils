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

    bool visit_elem_enter(void* userdata, cd_xml_doc_t* doc, cd_xml_ns_ix_t namespace_ix, cd_xml_stringview_t* name)
    {
        fprintf(stderr, "** <%.*s>\n", (int)(name->end - name->begin), name->begin);
        return true;
    }

    bool visit_elem_exit(void* userdata, cd_xml_doc_t* doc, cd_xml_ns_ix_t namespace_ix, cd_xml_stringview_t* name)
    {
        fprintf(stderr, "** </%.*s>\n", (int)(name->end - name->begin), name->begin);
        return true;
    }

    bool visit_attribute(void* userdata, cd_xml_doc_t* doc, cd_xml_ns_ix_t namespace_ix, cd_xml_stringview_t* name, cd_xml_stringview_t* value)
    {
        fprintf(stderr, "** attribute %.*s='%.*s'\n", (int)(name->end - name->begin), name->begin,
                (int)(value->end - value->begin), value->begin);
        return true;
    }

    bool visit_text(void* userdata, cd_xml_doc_t* doc, cd_xml_stringview_t* text)
    {
        fprintf(stderr, "** text '%.*s'\n", (int)(text->end - text->begin), text->begin);
        return true;
    }


}

int main(int argc, const char * argv[]) {
    {   // Namespaces
        const char* xml =
            "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
            "<quux />\n";
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
        
        cd_xml_apply_visitor(doc, nullptr,
                             visit_elem_enter,
                             visit_elem_exit,
                             visit_attribute,
                             visit_text);

        
        cd_xml_free(&doc);
    }
    {   // Namespaces
        const char* xml =
            "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
            "<foo xmlns='http://a.com'>\n"
            "  <b:bar xmlns:b='http://b.com'>\n"
            "    <c:baz xmlns:c='http://c.com' c:bah=''>\n"
            "     <b:quux/>\n"
            "    </c:baz>\n"
            "  </b:bar>\n"
            "  <baz xmlns:c='http://b.com'>\n"
            "  </baz>"
            "</foo>\n";
        cd_xml_doc_t* doc = NULL;
        cd_xml_parse_status_t rv = cd_xml_init_and_parse(&doc, xml, strlen(xml));
        assert(rv == CD_XML_STATUS_SUCCESS);
        cd_xml_write(doc, output_func, NULL, true);
        cd_xml_free(&doc);
    }

    return 0;
}
