# Noto Sans CJK subsets

These subsets are derived from [Noto CJK](https://github.com/notofonts/noto-cjk), licensed under the SIL Open Font License 1.1 in `OFL.txt`. The `sc` and `jp` character lists contain the non-Latin characters used by the current Simplified Chinese and Japanese draft catalogs. They are not full CJK fonts.

The upstream revision is `f8d157532fbfaeda587e826d4cd5b21a49186f7c`. The upstream files are `Sans/OTF/SimplifiedChinese/NotoSansCJKsc-{Regular,Bold}.otf` and `Sans/OTF/Japanese/NotoSansCJKjp-{Regular,Bold}.otf`. Their SHA-256 hashes are:

| Font | SHA-256 |
|---|---|
| sc Regular | `2c76254f6fc379fddfce0a7e84fb5385bb135d3e399294f6eeb6680d0365b74b` |
| sc Bold | `b5f0d1a190a7f9b43c310a8850630af12553df32c4c050543f9059732d9b4c0a` |
| jp Regular | `68a3fc98800b2a27b371f2fb79991daf3633bd89309d4ffaa6946fd587f375b5` |
| jp Bold | `e53dcb0dcb2922e45d01aae1ebd2f382bb81d4229b18b6b883bd170678af1f76` |

To rebuild a subset with fontTools 4.59.0, use the corresponding upstream file and character list:

```sh
python -m fontTools.subset NotoSansCJKsc-Regular.otf --unicodes-file=sc-characters.txt --output-file=sc-Regular.otf --name-IDs='*' --notdef-glyph --notdef-outline
```

Repeat for each locale and weight, then regenerate the glyph faces and browser catalog using the commands in `firmware/docs/architecture.md`. Adding translated characters requires updating the corresponding list and regenerating its subsets and faces.
