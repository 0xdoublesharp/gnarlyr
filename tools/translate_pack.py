#!/usr/bin/env python3
"""Translate a firmware language catalogue with a local Ollama server."""
import argparse
import collections
import hashlib
import re
import sys
import urllib.error
import urllib.request
import json
from pathlib import Path

LINE = re.compile(r'^(STR_[A-Z0-9_]+)\s*=\s*(.*)$')
PLACEHOLDER = re.compile(r'\{\d+\}')


def read_catalogue(path):
    result = []
    seen = set()
    for number, line in enumerate(path.read_text(encoding='utf-8').splitlines(), 1):
        if not line.strip() or line.lstrip().startswith('#'):
            continue
        match = LINE.fullmatch(line)
        if not match or match[1] in seen or not match[2].strip():
            raise ValueError(f'{path}:{number}: invalid, duplicate, or empty entry')
        seen.add(match[1])
        result.append((match[1], match[2].strip()))
    if not result:
        raise ValueError('empty catalogue')
    return result


def read_context(path):
    context = {}
    note = ""
    for line in path.read_text(encoding="utf-8").splitlines():
        if line.startswith("# "):
            note = line[2:].strip()
        match = LINE.fullmatch(line)
        if match:
            context[match[1]] = note
    return context


def make_prompt(text, language, key, section):
    if not PLACEHOLDER.search(text):
        section = section.split('.')[0]
    return (f"You are a professional English (en) to {language} translator. "
            f"Translate a short interface label for a portable music player. "
            f"The label identifier is {key}. Section note: {section}. "
            "Playback refers to audio, and volume controls are physical buttons. "
            f"Produce only the {language} translation, without explanations. "
            f"Preserve numbered placeholders exactly. Translate the following English text into {language}:\n\n\n{text}")


def validate(source, translated):
    errors = []
    if not translated.strip() or '\n' in translated or '\r' in translated:
        errors.append('translation must be a nonempty single line')
    if collections.Counter(PLACEHOLDER.findall(source)) != collections.Counter(PLACEHOLDER.findall(translated)):
        errors.append('placeholders differ')
    if source.count('\\n') != translated.count('\\n'):
        errors.append('line breaks differ')
    return errors


def build_pack(source, language, translate):
    entries = []
    for key, text in source:
        result = translate(text, language).strip()
        errors = validate(text, result)
        if errors:
            raise ValueError(f'{key}: {", ".join(errors)}')
        entries.append((key, result))
    return entries


def translate_entry(text, language, key, translate):
    errors = []
    for attempt in range(3):
        feedback = ', '.join(errors)
        value = '\\n'.join(translate(part, language, key, feedback).strip() for part in text.split('\\n'))
        errors = validate(text, value)
        if not errors:
            return value
        print(f'{key}: attempt {attempt + 1}/3: {", ".join(errors)}', file=sys.stderr, flush=True)
    raise ValueError(f'{key}: {", ".join(errors)}')


def checkpoint_token(source, language, model):
    data = json.dumps([source, language, model], ensure_ascii=False, separators=(',', ':'))
    return hashlib.sha256(data.encode('utf-8')).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--input', type=Path, default=Path('firmware/assets/lang/en.txt'))
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--resume', action='store_true', help='resume an existing partial catalog')
    parser.add_argument('--language', default='Spanish')
    parser.add_argument('--model', default='translategemma:27b')
    parser.add_argument('--url', default='http://127.0.0.1:11434')
    args = parser.parse_args()
    try:
        source = read_catalogue(args.input)
        context = read_context(args.input)
        token = checkpoint_token(source, args.language, args.model)
        if args.resume and args.output.read_text(encoding='utf-8').splitlines()[0] != f'# checkpoint: {token}':
            raise ValueError('checkpoint does not match source, language, or model')
        completed = read_catalogue(args.output) if args.resume else []
        if completed and [key for key, _ in completed] != [key for key, _ in source[:len(completed)]]:
            raise ValueError('partial catalog keys do not match input')
        if not args.resume and args.output.exists():
            raise ValueError(f'output already exists: {args.output}')
        for (key, text), (_, value) in zip(source, completed):
            if validate(text, value):
                raise ValueError(f'{key}: invalid existing translation')
        def translate(text, language, key, feedback=""):
            prompt = make_prompt(text, language, key, context.get(key, ""))
            if feedback:
                prompt = prompt.replace("Translate the following English text", f"Previous output failed validation: {feedback}. Do not add or remove placeholders. Translate the following English text")
            payload = json.dumps({'model': args.model, 'stream': False, 'prompt': prompt}).encode()
            request = urllib.request.Request(args.url.rstrip('/') + '/api/generate', data=payload, headers={'Content-Type': 'application/json'})
            with urllib.request.urlopen(request, timeout=600) as response:
                return json.load(response)['response']
        with args.output.open('a' if args.resume else 'x', encoding='utf-8') as output:
            if not args.resume:
                output.write(f'# checkpoint: {token}\n# Machine translated; review all entries before release.\n')
            for index, (key, text) in enumerate(source[len(completed):], len(completed) + 1):
                print(f'{index}/{len(source)} {key}', flush=True)
                value = translate_entry(text, args.language, key, translate)
                output.write(f'{key} = {value}\n')
                output.flush()
    except (OSError, ValueError, KeyError, TypeError, urllib.error.URLError) as exc:
        print(f'translation failed: {exc}', file=sys.stderr)
        return 1
    return 0


if __name__ == '__main__':
    sys.exit(main())
