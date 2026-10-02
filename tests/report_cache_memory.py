"""Read target ELF sections and DWARF member sizes; never infer from host sizeof."""
import argparse
import json
from elftools.elf.elffile import ELFFile

p=argparse.ArgumentParser()
p.add_argument('elf')
p.add_argument('output')
p.add_argument('--verify-packed',action='store_true')
args=p.parse_args()
with open(args.elf,'rb') as stream:
    elf=ELFFile(stream)
    sections={s.name:dict(bytes=s['sh_size'],address=hex(s['sh_addr'])) for s in elf.iter_sections()
        if s.name in ('.dram0.bss','.dram0.data','.flash.rodata','.flash.text','.iram0.text','.ext_ram.bss')}
    dwarf=elf.get_dwarf_info()
    names={'SynthEngine','VoiceAllocator','PreparedNote','PreparedNoteTable','UduCache',
        'UduVoice','InstrumentModelConfig','BodyResonator','ModalVoice'}
    types={}
    def size(die):
        if 'DW_AT_byte_size' in die.attributes: return die.attributes['DW_AT_byte_size'].value
        if die.tag=='DW_TAG_array_type':
            n=1
            for child in die.iter_children():
                a=child.attributes
                if 'DW_AT_upper_bound' in a: n*=a['DW_AT_upper_bound'].value+1
                elif 'DW_AT_count' in a: n*=a['DW_AT_count'].value
            return n*size(die.get_DIE_from_attribute('DW_AT_type'))
        if 'DW_AT_type' in die.attributes: return size(die.get_DIE_from_attribute('DW_AT_type'))
        return None
    for cu in dwarf.iter_CUs():
        for die in cu.iter_DIEs():
            a=die.attributes
            name=a.get('DW_AT_name')
            if not name: continue
            name=name.value.decode(errors='replace')
            if name not in names|{'CanonicalStrikePowCache'} or 'DW_AT_byte_size' not in a: continue
            members={}
            for child in die.iter_children():
                ca=child.attributes
                if child.tag=='DW_TAG_member' and 'DW_AT_data_member_location' in ca:
                    members[ca['DW_AT_name'].value.decode()]=dict(offset=ca['DW_AT_data_member_location'].value,
                        bytes=size(child.get_DIE_from_attribute('DW_AT_type')))
            types[name]=dict(bytes=size(die),members=members)
    assert names.issubset(types), names-set(types)
    symbols=[]
    for sym in elf.get_section_by_name('.symtab').iter_symbols():
        if sym['st_size'] and isinstance(sym['st_shndx'],int):
            section=elf.get_section(sym['st_shndx']).name
            if section in sections and section not in ('.flash.text','.iram0.text'):
                symbols.append(dict(name=sym.name,bytes=sym['st_size'],address=hex(sym['st_value']),section=section))
    result=dict(elf=args.elf,sections=sections,types=types,symbols=symbols)
    if args.verify_packed:
        assert types['SynthEngine']['bytes']<=82000, 'SynthEngine SRAM budget regression'
        assert types['PreparedNote']['bytes']==132 and types['UduCache']['bytes']==5928
        if 'CanonicalStrikePowCache' in types: assert types['CanonicalStrikePowCache']['bytes']==524
        tables=[v['bytes'] for k,v in types['SynthEngine']['members'].items() if k.endswith('PreparedNotes_')]
        assert len(tables)==9 and sum(tables)==60552, tables
    with open(args.output,'w') as out: json.dump(result,out,indent=2);out.write('\n')
    print(json.dumps(dict(sections=sections,types={k:v['bytes'] for k,v in types.items()}),indent=2))
