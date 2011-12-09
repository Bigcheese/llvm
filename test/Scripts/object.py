#!/usr/bin/env python

version     = "object 0.1.20111202"
description = ("")

import argparse

def dump(args):
  stream = file(args.input, 'rb')
  if args.target == 'coff':
    import object_coff
    object_coff.dump_coff(stream)

def yaml(args):
  stream = file(args.input, 'r')
  if args.target == 'coff':
    import object_coff
    obj = object_coff.make_coff(stream)
  obj.write()

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
