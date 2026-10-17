import sys, xml.etree.ElementTree as ET
f, out = sys.argv[1], sys.argv[2]
r = ET.parse(f).getroot()
rows=[]
def walk(el, tool):
    for c in el:
        if c.tag=='tool': walk(c, c.get('id'))
        elif c.tag=='option':
            oid=c.get('id')
            if oid is None: continue
            enums=[(e.get('id').split('.')[-1], e.get('command') or '', e.get('isDefault') or '') for e in c.findall('enumeratedOptionValue')]
            rows.append((tool or '', oid, c.get('valueType') or '', c.get('command') or '', c.get('commandFalse') or '', (c.get('defaultValue') or '') + ((' value='+c.get('value')) if c.get('value') else ''), c.get('superClass') or '', ' | '.join('%s=%s%s'%(a,b,' (default)' if d=='true' else '') for a,b,d in enums)))
        else: walk(c, tool)
walk(r, None)
with open(out,'w') as o:
    o.write('tool\toption id\tvalueType\tcommand\tcommandFalse\tdefaultValue\tsuperClass\tenum values (id suffix=flag)\n')
    for x in rows: o.write('\t'.join(x).replace('\n',' ')+'\n')
print(len(rows))
