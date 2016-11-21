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

atd_comment = re.compile(r'^ *//@atd ?(.*)')

def start(file):
    for line in file:
        m = atd_comment.match(line)
        if m:
            print(m.group(1))

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
