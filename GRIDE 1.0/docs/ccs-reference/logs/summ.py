import sys, re, xml.etree.ElementTree as ET
for f in sys.argv[1:]:
    print("########", f)
    r = ET.parse(f).getroot()
    for cc in r.iter('cconfiguration'):
        cfg = cc.find(".//configuration")
        print(" CONFIG", cfg.get('name'), "parent=", cfg.get('parent'), "ext=", cfg.get('artifactExtension'))
        for fi in cfg:
            print("  ", fi.tag, {k:v for k,v in fi.attrib.items() if k in ('resourcePath','name','excluding')})
            for tc in fi.iter('toolChain'):
                print("   toolChain super=", tc.get('superClass'))
            for el in fi.iter():
                if el.tag == 'tool':
                    print("   TOOL", el.get('name'), el.get('superClass'))
                if el.tag == 'option':
                    sc = el.get('superClass'); v = el.get('value'); vt = el.get('valueType')
                    lv = [x.get('value') for x in el.findall('listOptionValue')]
                    print("     ", sc, vt, repr(v) if v is not None else '', lv if lv else '')
                if el.tag in ('entry','sourceEntries'):
                    print("   ", el.tag, el.attrib)
