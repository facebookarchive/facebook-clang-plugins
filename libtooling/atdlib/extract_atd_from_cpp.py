# Copyright (c) 2014, Facebook, Inc.
# All rights reserved.
#
# This source code is licensed under the BSD-style license found in the
# LICENSE file in the root directory of this source tree. An additional grant
# of patent rights can be found in the PATENTS file in the same directory.

import sys
import argparse
import re

"""
Extract the ATD specifications inlined in a C/C++ file
"""

atd_begin = re.compile(r'^ */// *\\atd *(.*)')
atd_continue = re.compile(r'^ */// ?(.*)')
empty = re.compile(r'^ *$')
non_atd_comment = re.compile(r'^ *// ?(.*)')

def start(file):
    for line in file:
        m = atd_begin.match(line)
        if m:
            tail = m.group(1)
            if tail:
                print(tail)
            atd(file)

def atd(file):
    for line in file:
        m = atd_begin.match(line)
        if not m:
            m = atd_continue.match(line)
        if m:
            print(m.group(1))
        else:
            if empty.match(line):
                continue
            elif non_atd_comment.match(line):
                continue
            else:
                break

def main():
    arg_parser = argparse.ArgumentParser(description='Extract the ATD specifications inlined in a C/C++ file')
    arg_parser.add_argument(metavar="FILE", nargs='?', dest="input_file", help="Input log file (default: stdin)")
    args = arg_parser.parse_args()
    if args.input_file:
        file = open(args.input_file, "r")
    else:
        file = sys.stdin
    start(file)

if __name__ == '__main__':
    main()
