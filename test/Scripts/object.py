#!/usr/bin/env python

version     = "object 0.1.20111202"
description = ("")

import argparse

def dump(args):
  pass

def yaml(args):
  import yaml, types, struct

  def get(d, key, default):
    v = d.get(key, default)
    if v is None:
      v = default
    return v

  def default(v, d):
    if v is None:
      return d
    return v

  SIZEOF_HEADER  = 20
  SIZEOF_SECTION = 40
  SIZEOF_SYMBOL  = 18

  SecChars = {
      'IMAGE_SCN_CNT_CODE':      0x00000020,
      'IMAGE_SCN_ALIGN_16BYTES': 0x00500000,
      'IMAGE_SCN_MEM_EXECUTE':   0x20000000,
      'IMAGE_SCN_MEM_READ':      0x40000000
    }

  SimpleType = {
      'IMAGE_SYM_TYPE_NULL': 0
    }

  ComplexType = {
      'IMAGE_SYM_DTYPE_NULL': 0,
      'IMAGE_SYM_DTYPE_FUNCTION': 2
    }

  StorageClass = {
      'IMAGE_SYM_CLASS_EXTERNAL': 2,
      'IMAGE_SYM_CLASS_STATIC': 3
    }

  class COFFHeader(yaml.YAMLObject):
    yaml_tag = u'!Header'
    Machine              = None
    NumberOfSections     = None
    TimeDateStamp        = None
    PointerToSymbolTable = None
    NumberOfSymbols      = None
    SizeOfOptionalHeader = None
    Characteristics      = None

    def layout(self, coff):
      self.Machine = default(self.Machine, 0)
      self.NumberOfSections = default(self.NumberOfSections, len(coff.sections))
      self.TimeDateStamp = default(self.TimeDateStamp, 0)
      self.SizeOfOptionalHeader = default(self.SizeOfOptionalHeader, 0)
      self.PointerToSymbolTable = default(self.PointerToSymbolTable,
              (SIZEOF_HEADER + self.SizeOfOptionalHeader) + \
              (self.NumberOfSections * SIZEOF_SECTION))
      self.NumberOfSymbols = default(self.NumberOfSymbols, len(coff.symbols))
      self.Characteristics = default(self.Characteristics, 0)

    def write(self):
      return [(struct.pack('<H', self.Machine), 'Machine'),
              (struct.pack('<H', self.NumberOfSections), 'NumberOfSections'),
              (struct.pack('<I', self.TimeDateStamp), 'TimeDateStamp'),
              (struct.pack('<I', self.PointerToSymbolTable),
                'PointerToSymbolTable'),
              (struct.pack('<I', self.NumberOfSymbols), 'NumberOfSymbols'),
              (struct.pack('<H', self.SizeOfOptionalHeader),
                'SizeOfOptionalHeader'),
              (struct.pack('<H', self.Characteristics), 'Characteristics')]

  class COFFSection(yaml.YAMLObject):
    yaml_tag = u'!Section'
    Index                = None

    Name                 = None
    VirtualSize          = None
    VirtualAddress       = None
    SizeOfRawData        = None
    PointerToRawData     = None
    PointerToRelocations = None
    PointerToLineNumbers = None
    NumberOfRelocations  = None
    NumberOfLineNumbers  = None
    Characteristics      = None
    SectionData          = None
    Relocations          = None

    def layout(self, coff, index):
      self.Index = default(self.Index, index)
      if type(self.Name) == types.StringType:
        if len(self.Name) > 8:
          self.Name = '/%d' % coff.strtab.add(self.Name)
      else:
        self.Name = '/%d' % self.Name
      self.VirtualSize = default(self.VirtualSize, 0)
      self.VirtualAddress = default(self.VirtualAddress, 0)
      self.SizeOfRawData = default(self.SizeOfRawData, len(self.SectionData))
      self.PointerToRawData = default(self.PointerToRawData, 0)
      self.PointerToRelocations = default(self.PointerToRelocations, 0)
      self.PointerToLineNumbers = default(self.PointerToLineNumbers, 0)
      self.NumberOfRelocations = default(self.NumberOfRelocations, 0)
      self.NumberOfLineNumbers = default(self.NumberOfLineNumbers, 0)
      self.Characteristics = default(self.Characteristics, 0)

      if type(self.Characteristics) == types.ListType:
        res = 0
        for c in self.Characteristics:
          res |= SecChars[c]
        self.Characteristics = res

    def decode_name(self, coff, name):
      if name[0] == '/':
        return coff.strtab.get(int(name[1:]))
      else:
        return name

    def write_header(self, coff):
      return [(None, 'Section %d' % self.Index),
              (struct.pack('8s', self.Name),
                'Name: %s' % self.decode_name(coff, self.Name)),
              (struct.pack('<I', self.VirtualSize), 'VirtualSize'),
              (struct.pack('<I', self.VirtualAddress), 'VirtualAddress'),
              (struct.pack('<I', self.SizeOfRawData), 'SizeOfRawData'),
              (struct.pack('<I', self.PointerToRawData), 'PointerToRawData'),
              (struct.pack('<I', self.PointerToRelocations),
                'PointerToRelocations'),
              (struct.pack('<I', self.PointerToLineNumbers),
                'PointerToLineNumbers'),
              (struct.pack('<H', self.NumberOfRelocations),
                'NumberOfRelocations'),
              (struct.pack('<H', self.NumberOfLineNumbers),
                'NumberOfLineNumbers'),
              (struct.pack('<I', self.Characteristics), 'Characteristics')]

    def write_contents(self):
      return [(None, 'Section %d Data' % self.Index),
              (self.SectionData, 'Data')]

    def write_relocations(self):
      return [(None, None)]

  class COFFRelocation(yaml.YAMLObject):
    yaml_tag = u'!Relocation'
    VirtualAddress   = None
    SymbolTableIndex = None
    Type             = None

    def layout(self, coff):
      pass

  class COFFSymbol(yaml.YAMLObject):
    yaml_tag = u'!Symbol'
    Index              = None

    Name               = None
    Value              = None
    SectionNumber      = None
    SimpleType         = None
    ComplexType        = None
    StorageClass       = None
    NumberOfAuxSymbols = None
    AuxillaryData      = None

    def layout(self, coff):
      if type(self.Name) == types.StringType:
        if len(self.Name) > 8:
          self.Name = struct.pack('<II', 0, coff.strtab.add(self.Name))
      else:
        self.Name = struct.pack('<II', 0, self.Name)
      self.Value = default(self.Value, 0)
      self.SectionNumber = default(self.SectionNumber, 0)
      self.SimpleType = default(self.SimpleType, 0)
      self.ComplexType = default(self.ComplexType, 0)
      self.StorageClass = default(self.StorageClass, 0)
      self.NumberOfAuxSymbols = default(self.NumberOfAuxSymbols, 0)
      self.AuxillaryData = default(self.AuxillaryData, "")

      if type(self.SimpleType) == types.StringType:
        self.SimpleType = SimpleType[self.SimpleType]
      if type(self.ComplexType) == types.StringType:
        self.ComplexType = ComplexType[self.ComplexType]
      if type(self.StorageClass) == types.StringType:
        self.StorageClass = StorageClass[self.StorageClass]

    def decode_name(self, coff, name):
      if name[0] != 0:
        return name
      else:
        return coff.strtab.get(struct.unpack('<II', name)[1])

    def write(self, coff):
      return [(None, 'Symbol %d' % self.Index),
              (struct.pack('8s', self.Name),
                'Name: %s' % self.decode_name(coff, self.Name)),
              (struct.pack('<I', self.Value), 'Value'),
              (struct.pack('<H', self.SectionNumber), 'SectionNumber'),
              (struct.pack('<H', self.SimpleType | (self.ComplexType << 4)),
                'Type'),
              (struct.pack('<B', self.StorageClass), 'StorageClass'),
              (struct.pack('<B', self.NumberOfAuxSymbols),
                'NumberOfAuxSymbols'),
              (self.AuxillaryData, 'Auxillary Data')]

  class COFFStringTable:
    def __init__(self, strings):
      self.offset = 4 # Reserve space for size info
      self.htab   = {}
      if type(strings) == types.StringType:
        self.add(strings)
      elif type(strings) == types.ListType:
        for s in strings:
          self.add(s)

    def add(self, string):
      try:
        return self.lookup(string)
      except KeyError:
        self.htab[string] = offset = self.offset
        self.offset += len(string) + 1
        return offset

    def lookup(self, string):
      return self.htab[string]

    def get(self, index):
      for k, v in self.htab.iteritems():
        if v == index:
          return k
      raise ValueError

    def write(self):
      ret = [(None, 'String Table'),
             (struct.pack('<I', self.offset), 'Size')]
      l = self.htab.items()
      l.sort(lambda x, y: cmp(x[1], y[1]))
      for (ss, oo) in l:
        ret.append((ss + '\0', ss))
      return ret

  def write_hexbytes_comment(data):
    for p in data:
      if p[0] is not None:
        for c in p[0]:
          print '%02X' % ord(c),
      if p[1] is not None:
        print('# ' + p[1])

  class COFF:
    def __init__(self, yamldef):
      self.yd = yamldef
      self.header = get(self.yd, 'header', COFFHeader())
      self.sections = get(self.yd, 'sections', [])
      self.symbols = get(self.yd, 'symbols', [])
      self.strtab = COFFStringTable(get(self.yd, 'strtab', []))

    def layout(self):
      self.header.layout(self)

      curoffset = self.header.PointerToSymbolTable + \
                    self.header.NumberOfSymbols * SIZEOF_SYMBOL
      for i, s in enumerate(self.sections):
        s.layout(self, i)
        if s.PointerToRawData == 0 and s.SizeOfRawData != 0:
          s.PointerToRawData = curoffset
          curoffset += s.SizeOfRawData

      index = 0
      for s in self.symbols:
        s.Index = index
        s.layout(self)
        index += 1 + s.NumberOfAuxSymbols

    def write(self):
      # Write out header.
      write_hexbytes_comment(self.header.write())
      # Write out section headers.
      for sec in self.sections:
        write_hexbytes_comment(sec.write_header(self))
      # Write out symbol table.
      for symb in self.symbols:
        write_hexbytes_comment(symb.write(self))
      # Write out section data.
      for sec in self.sections:
        write_hexbytes_comment(sec.write_contents())
      # Write out relocations.
      for sec in self.sections:
        write_hexbytes_comment(sec.write_relocations())
      # Write out strtab
      write_hexbytes_comment(self.strtab.write())

  def make_coff(stream):
    return COFF(yaml.load(stream))

  stream = file(args.input, 'r')
  coff = make_coff(stream)
  coff.layout()
  coff.write()

def hexbytes(args):
  import struct

  def ishex(c):
    o = ord(c)
    if (o >= 48 and o <= 57) or (o >= 65 and o <= 70) or (o >= 97 and o <= 102):
      return True
    return False

  stream = file(args.input, 'r')
  with open(args.output, 'wb') as f:
    for l in stream:
      comment = l.find('#')
      if comment != -1:
        l = l[:l.find('#')]
      l = [c for c in l if ishex(c)]
      if len(l) == 0:
        continue
      bytes = [str(l[i]) + str(l[i + 1]) for i in xrange(0, len(l), 2)]
      for b in bytes:
        f.write(struct.pack('B', int(b, 16)))

if __name__ == '__main__':
  parser = argparse.ArgumentParser( version=version
                                  , description=description)
  subparsers = parser.add_subparsers(help='sub-command help')

  io_parser = argparse.ArgumentParser(add_help=False)
  io_parser.add_argument('input')
  io_parser.add_argument('-o', metavar='FILE',
                         help='write output to FILE [default: stdout]',
                         dest='output', default='-')

  target_parser = argparse.ArgumentParser(add_help=False)
  target_parser.add_argument('-t', required=True, dest='target', choices={'elf',
                             'coff', 'macho'})

  parser_dump = subparsers.add_parser('dump', help='Dump object files to YAML',
                                      parents=[io_parser, target_parser])
  parser_dump.set_defaults(func=dump)

  parser_yaml = subparsers.add_parser('yaml', help=('Build an object file from'
                                      'a YAML description'),
                                      parents=[io_parser, target_parser])
  parser_yaml.set_defaults(func=yaml)

  parser_hexbytes = subparsers.add_parser('hexbytes', help=('Convert a hexbytes'
                                          'file into a binary file'),
                                          parents=[io_parser])
  parser_hexbytes.set_defaults(func=hexbytes)

  args = parser.parse_args()
  args.func(args)
