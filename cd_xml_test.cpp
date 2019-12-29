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
    cd_xml_flags_t flags = CD_XML_FLAGS_NONE;

    {   // XML from scratch

        cd_xml_doc_t* doc = cd_xml_init();
        assert(doc);

        auto foo_str = cd_xml_strv("foo");
        auto bar_str = cd_xml_strv("bar");
        auto baz_str = cd_xml_strv("baz");
        auto quux_str = cd_xml_strv("quux");

        auto foo = cd_xml_add_element(doc, cd_xml_no_ix, &foo_str, cd_xml_no_ix, CD_XML_FLAGS_COPY_STRINGS);
        auto bar = cd_xml_add_element(doc, cd_xml_no_ix, &bar_str, foo, CD_XML_FLAGS_COPY_STRINGS);
        cd_xml_add_attribute(doc, cd_xml_no_ix, &baz_str, &quux_str, bar, CD_XML_FLAGS_COPY_STRINGS);
        cd_xml_add_text(doc, &quux_str, foo, CD_XML_FLAGS_COPY_STRINGS);

        cd_xml_write(doc, output_func, NULL, true);
        cd_xml_write(doc, output_func, NULL, false);
        cd_xml_free(&doc);
    }

    {   // Namespaces
        const char* xml =
            "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
            "<quux />\n";
        cd_xml_doc_t* doc = NULL;
        auto rv = cd_xml_init_and_parse(&doc, xml, std::strlen(xml), flags);
        assert(rv == CD_XML_STATUS_SUCCESS);
        cd_xml_write(doc, output_func, nullptr, true);
        cd_xml_write(doc, output_func, NULL, false);
        cd_xml_free(&doc);
    }
    {   // Misc UTF-tests
        const char* xml =
            "<?xml version=\"1.0\" encoding=\"UTF-8\"?><!--m oo-->"
            "<foo moo=' doo  '><gah quux='waldo&lt;&#x5d0;'>   ᚠᚢᚦ&amp;ᚨᚱᚲ €  <meep/> æøå 𠜎  </gah><meh>meh</meh></foo>";
        cd_xml_doc_t* doc = NULL;
        auto rv = cd_xml_init_and_parse(&doc, xml, std::strlen(xml), flags);
        assert(rv == CD_XML_STATUS_SUCCESS);
        cd_xml_write(doc, output_func, NULL, true);
        cd_xml_write(doc, output_func, NULL, false);
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
        cd_xml_parse_status_t rv = cd_xml_init_and_parse(&doc, xml, strlen(xml), flags);
        assert(rv == CD_XML_STATUS_SUCCESS);
        cd_xml_write(doc, output_func, NULL, true);
        cd_xml_write(doc, output_func, NULL, false);
        cd_xml_free(&doc);
    }

    return 0;
}
