#!/usr/bin/env python3
"""Generate draft catalogs and compile packs, continuing after failures."""
import argparse
import subprocess
import sys
from pathlib import Path

LANGUAGES = (
    ('fr', 'French', 'Français'), ('af', 'Afrikaans', 'Afrikaans'), ('ca', 'Catalan', 'Català'),
    ('da', 'Danish', 'Dansk'), ('de', 'German', 'Deutsch'),
    ('es', 'Spanish', 'Español'), ('fi', 'Finnish', 'Suomi'),
    ('id', 'Indonesian', 'Bahasa Indonesia'),
    ('is', 'Icelandic', 'Íslenska'), ('it', 'Italian', 'Italiano'),
    ('ms', 'Malay', 'Bahasa Melayu'), ('nl', 'Dutch', 'Nederlands'),
    ('no', 'Norwegian', 'Norsk'), ('pt-BR', 'Brazilian Portuguese', 'Português brasileiro'),
    ('sv', 'Swedish', 'Svenska'),
    ('ar', 'Arabic', 'العربية'), ('am', 'Amharic', 'አማርኛ'),
    ('bn', 'Bengali', 'বাংলা'), ('bg', 'Bulgarian', 'Български'),
    ('bs', 'Bosnian', 'Bosanski'), ('zh-CN', 'Simplified Chinese', '简体中文'),
    ('zh-TW', 'Traditional Chinese', '繁體中文'), ('hr', 'Croatian', 'Hrvatski'),
    ('cs', 'Czech', 'Čeština'), ('el', 'Greek', 'Ελληνικά'),
    ('gu', 'Gujarati', 'ગુજરાતી'), ('ha', 'Hausa', 'Hausa'),
    ('he', 'Hebrew', 'עברית'), ('hi', 'Hindi', 'हिन्दी'),
    ('hu', 'Hungarian', 'Magyar'), ('ja', 'Japanese', '日本語'),
    ('kn', 'Kannada', 'ಕನ್ನಡ'), ('km', 'Khmer', 'ខ្មែរ'),
    ('ko', 'Korean', '한국어'), ('ml', 'Malayalam', 'മലയാളം'),
    ('mr', 'Marathi', 'मराठी'), ('ne', 'Nepali', 'नेपाली'),
    ('fa', 'Persian', 'فارسی'), ('pl', 'Polish', 'Polski'),
    ('pa', 'Punjabi', 'ਪੰਜਾਬੀ'), ('ro', 'Romanian', 'Română'),
    ('ru', 'Russian', 'Русский'), ('sr', 'Serbian', 'Српски'),
    ('si', 'Sinhala', 'සිංහල'), ('sk', 'Slovak', 'Slovenčina'),
    ('sl', 'Slovenian', 'Slovenščina'), ('sw', 'Swahili', 'Kiswahili'),
    ('tl', 'Filipino', 'Filipino'), ('ta', 'Tamil', 'தமிழ்'),
    ('te', 'Telugu', 'తెలుగు'), ('th', 'Thai', 'ไทย'),
    ('tr', 'Turkish', 'Türkçe'), ('uk', 'Ukrainian', 'Українська'),
    ('ur', 'Urdu', 'اردو'), ('vi', 'Vietnamese', 'Tiếng Việt'),
    ('yo', 'Yoruba', 'Yorùbá'),
)
ROOT = Path(__file__).resolve().parents[1]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--input', type=Path, required=True)
    parser.add_argument('--output-dir', type=Path, required=True)
    parser.add_argument('--languages', nargs='*', default=[code for code, _, _ in LANGUAGES])
    args = parser.parse_args()
    selected = [entry for entry in LANGUAGES if entry[0] in args.languages]
    if set(args.languages) != {entry[0] for entry in selected}:
        parser.error('unknown language code')
    args.output_dir.mkdir(parents=True, exist_ok=True)
    source = args.input.read_bytes()
    failures = []
    for code, language, native in selected:
        folder = args.output_dir / code
        folder.mkdir(exist_ok=True)
        english = folder / 'en.txt'
        if english.exists() and english.read_bytes() != source:
            print(f'{code}: catalog changed; use a new output directory', flush=True)
            failures.append(code)
            continue
        if not english.exists():
            english.write_bytes(source)
        draft = folder / f'{code}.txt'
        command = [sys.executable, str(ROOT / 'tools/translate_pack.py'), '--input', str(english), '--output', str(draft), '--language', language]
        if draft.exists():
            command.append('--resume')
        print(f'Starting {language}', flush=True)
        if subprocess.run(command).returncode:
            failures.append(code)
            continue
        lines = draft.read_text(encoding='utf-8').splitlines()
        lines = [f'STR_LANGUAGE_NAME = {native}' if line.startswith('STR_LANGUAGE_NAME = ') else line for line in lines]
        draft.write_text('\n'.join(lines) + '\n', encoding='utf-8')
        pack = folder / f'{code}.pack'
        if pack.exists():
            print(f'{code}: pack already exists; leaving it untouched', flush=True)
            failures.append(code)
            continue
        command = [sys.executable, str(ROOT / 'firmware/tools/lang_pack.py'), '--languages', str(folder), '--pack', code, '--output', str(pack)]
        if subprocess.run(command).returncode:
            failures.append(code)
    print('Failed: ' + (', '.join(failures) if failures else 'none'), flush=True)
    return bool(failures)


if __name__ == '__main__':
    sys.exit(main())
