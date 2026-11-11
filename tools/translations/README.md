# Local language catalog generation

After the language-support branch is merged, start Ollama with `translategemma:27b` installed. Configure its model storage according to the project's disk policy before downloading. From the repository root:

```sh
python3 tools/translate_pack.py --output /Volumes/Scratch/SnowskyEcho/es.txt
python3 tools/translate_pack.py --language French --output /Volumes/Scratch/SnowskyEcho/fr.txt
```

The input defaults to `firmware/assets/lang/en.txt`. Supply another catalog using `--input`, a target language with `--language`, and optionally an Ollama model or server with `--model` or `--url`. The tool supplies music-player context, each string ID, and its nearest catalog section note to the model. It translates one string at a time and checks numbered placeholders and escaped line breaks. It writes validated entries incrementally and stops on an error. Use `--resume` with the same input and output to continue a partial catalog; existing output is otherwise never overwritten.

The output is an unreviewed source catalog, not a compiled pack. Have a fluent speaker review the translations, especially ambiguous labels, and verify font coverage and display fit. Follow the language-pack build instructions in `firmware/docs/architecture.md` to validate and compile the reviewed catalog. Those checks do not establish translation quality or device layout.
