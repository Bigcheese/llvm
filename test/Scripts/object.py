#!/usr/bin/env python

version     = "object 0.1.20111202"
description = ("")

import argparse

def dump(args):
  pass

def yaml(args):
  import yaml
  class ObjectParser:
    def load(self, stream):
      return yaml.load(stream)

  class COFFHeader(yaml.YAMLObject):
    yaml_tag = u'!Header'
    Machine              = 0
    NumberOfSections     = 0
    TimeDateStamp        = 0
    PointerToSymbolTable = 0
    NumberOfSymbols      = 0
    SizeOfOptionalHeader = 0
    Characteristics      = 0

    def __init__(self, Machine, NumberOfSections, TimeDateStamp,
                 PointerToSymbolTable, NumberOfSymbols, SizeOfOptionalHeader,
                 Characteristics):
      self.Machine              = Machine
      self.NumberOfSections     = NumberOfSections
      self.TimeDateStamp        = TimeDateStamp
      self.PointerToSymbolTable = PointerToSymbolTable
      self.NumberOfSymbols      = NumberOfSymbols
      self.SizeOfOptionalHeader = SizeOfOptionalHeader
      self.Characteristics      = Characteristics

  class COFFParser(ObjectParser):
    pass

  def make_coff(stream):
    c = COFFParser()
    return c.load(stream)

  stream = file(args.input, 'r')
  print make_coff(stream)

def hexbytes(args):
  pass

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
