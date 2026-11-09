#!/usr/bin/env python3
"""Verify Ghidra loaded the analysis ELF's original bytes at the intended addresses."""
import argparse
import json
from pathlib import Path


def check(mapping: dict, analysis: dict) -> tuple[int, int]:
    if analysis['import_sha256'] != mapping['elf_sha256']:
        raise ValueError('Imported ELF digest differs from mapped export')
    if analysis['language'] != 'ARM:LE:32:Cortex':
        raise ValueError('Incorrect Ghidra processor language')
    blocks={b['name']: b for b in analysis['memory']}
    for region in mapping['regions']:
        block=blocks['.'+region['name']]
        if int(block['start'],16)!=region['address'] or block['size']!=region['size']:
            raise ValueError('Incorrect loaded extent: '+region['name'])
        if block['initialized'] != (region['file_offset'] is not None):
            raise ValueError('Incorrect initialized/BSS state: '+region['name'])
        if region['file_offset'] is not None and block.get('sha256')!=region['sha256']:
            raise ValueError('Ghidra memory differs from original firmware: '+region['name'])
    if len(analysis['functions']) < 2:
        raise ValueError('At least two selected functions must be checked')
    for function in analysis['functions']:
        if function['thumb_mode'] != 1 or not function['decompile_completed'] or function['instructions'] < 1:
            raise ValueError('Selected function did not decode/decompile in Thumb mode')
    return len(mapping['regions']),len(analysis['functions'])


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('mapping',type=Path)
    p.add_argument('analysis',type=Path)
    args=p.parse_args()
    regions,functions=check(json.loads(args.mapping.read_text()),json.loads(args.analysis.read_text()))
    print(f'PASS: imported digest, {regions} mapped regions, original byte hashes, and {functions} Thumb functions')

if __name__=='__main__': main()
