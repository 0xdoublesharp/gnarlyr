(() => {
  "use strict";
  const encoder = new TextEncoder();
  const decoder = new TextDecoder("utf-8", {fatal: true, ignoreBOM: true});
  const headerBytes = 56;

  function fail(message) {
    throw new Error(message);
  }

  function catalogue(value) {
    const current = value || globalThis.LANGUAGE_CATALOGUE;
    if (!current || !Array.isArray(current.entries) || !Array.isArray(current.glyphs)) {
      fail("The language template could not load. Reload this page and try again.");
    }
    return current;
  }

  function encode(text, label) {
    for (const character of text) {
      const value = character.codePointAt(0);
      if (value >= 0xD800 && value <= 0xDFFF) fail(`${label} contains invalid text.`);
    }
    return encoder.encode(text);
  }

  function decode(bytes, label) {
    try {
      return decoder.decode(bytes);
    } catch {
      return fail(`${label} is not valid UTF-8 text.`);
    }
  }

  function checkCode(code, info) {
    if (!code || /[^A-Za-z0-9-]/.test(code) || code.length > info.code_limit) {
      fail(`Use a language code of 1–${info.code_limit} letters, digits or hyphens, such as es or pt-BR.`);
    }
  }

  function checkName(name, info) {
    if (!name || [...name].some(character => character !== " " && /[\p{C}\p{Z}]/u.test(character))) {
      fail("Use a nonempty native language name on one printable line.");
    }
    if (encode(name, "Language name").length >= info.name_bytes) {
      fail(`The native language name must fit in ${info.name_bytes - 1} UTF-8 bytes.`);
    }
  }

  function placeholders(text) {
    return [...new Set([...text.matchAll(/\{(\d)\}/g)].map(match => match[1]))].sort().join(",");
  }

  function checkTexts(texts, info) {
    const drawable = new Set(info.glyphs);
    for (const entry of info.entries) {
      const text = texts[entry.id];
      if (!text) fail(`${entry.id} must not be empty.`);
      if (placeholders(text) !== placeholders(entry.english)) {
        fail(`${entry.id} must keep the placeholders from its English text.`);
      }
      for (const character of text) {
        const point = character.codePointAt(0);
        if (character !== "\n" && !drawable.has(point)) {
          fail(`${entry.id} uses a character the player cannot display: U+${point.toString(16).toUpperCase().padStart(4, "0")}.`);
        }
      }
      encode(text, entry.id);
    }
  }

  function trimText(text) {
    return text.replace(/^[\p{White_Space}\x1c-\x1f]+|[\p{White_Space}\x1c-\x1f]+$/gu, "");
  }

  function parseTranslation(text, value) {
    const info = catalogue(value);
    const known = new Set(info.entries.map(entry => entry.id));
    const result = Object.create(null);
    const lines = text.split(/\r\n|[\n\v\f\r\x1c-\x1e\u0085\u2028\u2029]/);
    lines.forEach((line, index) => {
      if (!trimText(line) || trimText(line).startsWith("#")) return;
      const match = /^(STR_[A-Z0-9_]+)\s*=\s*(.*)$/.exec(line);
      if (!match) fail(`Line ${index + 1}: use STR_NAME = translated text.`);
      const id = match[1];
      if (!known.has(id)) fail(`Line ${index + 1}: ${id} is not in this firmware's template.`);
      if (Object.hasOwn(result, id)) fail(`Line ${index + 1}: ${id} appears twice.`);
      result[id] = trimText(match[2]).replaceAll("\\n", "\n");
    });
    return result;
  }

  function fnv(bytes, start = 2166136261) {
    let result = start;
    for (const byte of bytes) result = Math.imul(result ^ byte, 16777619) >>> 0;
    return result;
  }

  function checksum(bytes) {
    return fnv(bytes.subarray(20), fnv(bytes.subarray(0, 16)));
  }

  function buildPack(code, name, text, value) {
    const info = catalogue(value);
    checkCode(code, info);
    checkName(name, info);
    const translations = parseTranslation(text, info);
    if (translations[info.name_id] && translations[info.name_id] !== name) {
      fail(`The native name must match ${info.name_id} in your translation.`);
    }
    translations[info.name_id] = name;
    const texts = Object.fromEntries(info.entries.map(entry => [entry.id, translations[entry.id] || entry.english]));
    checkTexts(texts, info);
    const strings = info.entries.map(entry => encode(texts[entry.id], entry.id));
    const tableEnd = headerBytes + info.entries.length * 2;
    const size = tableEnd + strings.reduce((total, item) => total + item.length + 1, 0);
    if (size > info.pack_limit) fail(`This pack is ${size.toLocaleString()} bytes. Shorten the text to fit ${info.pack_limit.toLocaleString()} bytes.`);
    const bytes = new Uint8Array(size);
    const view = new DataView(bytes.buffer);
    bytes.set(encode(info.magic, "Pack format"));
    view.setUint16(4, info.version, true);
    view.setUint16(6, info.entries.length, true);
    view.setUint32(8, size, true);
    view.setUint32(12, info.catalogue_hash, true);
    bytes.set(encode(code, "Language code"), 20);
    bytes.set(encode(name, "Language name"), 20 + info.code_bytes);
    let cursor = tableEnd;
    strings.forEach((item, index) => {
      view.setUint16(headerBytes + index * 2, cursor - tableEnd, true);
      bytes.set(item, cursor);
      cursor += item.length + 1;
    });
    view.setUint32(16, checksum(bytes), true);
    return bytes;
  }

  function metadata(bytes, label) {
    const end = bytes.indexOf(0);
    if (end <= 0 || bytes.subarray(end + 1).some(byte => byte !== 0)) {
      fail(`${label} must be nonempty and zero-padded.`);
    }
    return decode(bytes.subarray(0, end), label);
  }

  function inspectPack(data, value) {
    const info = catalogue(value);
    const bytes = data instanceof ArrayBuffer ? new Uint8Array(data) : Uint8Array.from(data);
    if (bytes.length < headerBytes) fail("This file is shorter than a language-pack header.");
    const view = new DataView(bytes.buffer, bytes.byteOffset, bytes.byteLength);
    const count = view.getUint16(6, true);
    const size = view.getUint32(8, true);
    if (decode(bytes.subarray(0, 4), "Pack format") !== info.magic || view.getUint16(4, true) !== info.version) {
      fail("This is not a supported language pack.");
    }
    if (size !== bytes.length || size > info.pack_limit || size <= headerBytes + count * 2) {
      fail("This language pack has an invalid size or is incomplete.");
    }
    if (view.getUint32(16, true) !== checksum(bytes)) fail("This language pack's checksum does not match. Copy or build it again.");
    if (count !== info.entries.length || view.getUint32(12, true) !== info.catalogue_hash) {
      fail("This pack belongs to a different firmware template. Rebuild it with the current template.");
    }
    const code = metadata(bytes.subarray(20, 20 + info.code_bytes), "Language code");
    const name = metadata(bytes.subarray(20 + info.code_bytes, headerBytes), "Language name");
    checkCode(code, info);
    checkName(name, info);
    const body = bytes.subarray(headerBytes + count * 2);
    if (body[body.length - 1] !== 0) fail("This language pack's text is incomplete.");
    decode(body, "Pack text");
    const texts = Object.create(null);
    info.entries.forEach((entry, index) => {
      const offset = view.getUint16(headerBytes + index * 2, true);
      if (offset >= body.length || (offset > 0 && body[offset - 1] !== 0)) fail(`${entry.id} has an invalid text offset.`);
      const end = body.indexOf(0, offset);
      if (end < 0) fail(`${entry.id} is incomplete.`);
      texts[entry.id] = decode(body.subarray(offset, end), entry.id);
    });
    if (texts[info.name_id] !== name) fail("The pack's native name does not match its language-name text.");
    checkTexts(texts, info);
    return {code, name, count, size, catalogue_hash: info.catalogue_hash, texts};
  }

  function templateText(value) {
    const info = catalogue(value);
    return "# Translate the text after '='. Keep names and {0} placeholders.\n# Missing or empty entries use English. Write \\n for a line break.\n\n" +
      info.entries.map(entry => `${entry.id} = ${entry.english.replaceAll("\n", "\\n")}`).join("\n") + "\n";
  }

  globalThis.LanguagePacks = Object.freeze({parseTranslation, buildPack, inspectPack, templateText});
  if (typeof document === "undefined") return;
  const form = document.getElementById("language-builder");
  if (!form) return;
  const byId = id => document.getElementById(id);
  const codeInput = byId("language-code");
  const nameInput = byId("language-name");
  const textInput = byId("translation-text");
  const error = byId("language-error");
  const ready = byId("pack-ready");
  const download = byId("download-pack");
  let pending = null;
  let imported = null;
  let reading = 0;

  function showError(message) {
    error.textContent = message;
    error.hidden = false;
  }

  function invalidate() {
    reading++;
    pending = null;
    ready.hidden = true;
    download.disabled = true;
    error.hidden = true;
  }

  function saveFile(name, bytes, type) {
    const url = URL.createObjectURL(new Blob([bytes], {type}));
    const link = document.createElement("a");
    link.href = url;
    link.download = name;
    document.body.append(link);
    link.click();
    link.remove();
    setTimeout(() => URL.revokeObjectURL(url), 1000);
  }

  async function readFile(file, limit) {
    if (!file) return null;
    if (file.size > limit) fail(`Choose a file smaller than ${limit.toLocaleString()} bytes.`);
    return new Uint8Array(await file.arrayBuffer());
  }

  form.addEventListener("input", invalidate);
  nameInput.addEventListener("input", () => {
    const id = catalogue().name_id;
    textInput.value = textInput.value.replace(new RegExp(`^${id}\\s*=.*$`, "m"), () => `${id} = ${nameInput.value}`);
  });
  textInput.addEventListener("input", () => {
    try {
      const translations = parseTranslation(textInput.value);
      if (translations[catalogue().name_id]) nameInput.value = translations[catalogue().name_id];
    } catch {
      return;
    }
  });
  form.addEventListener("submit", event => {
    event.preventDefault();
    invalidate();
    try {
      const bytes = buildPack(codeInput.value.trim(), nameInput.value.trim(), textInput.value);
      const pack = inspectPack(bytes);
      pending = {bytes, code: pack.code};
      byId("pack-summary").textContent = `${pack.name} · ${pack.code} · ${pack.size.toLocaleString()} bytes`;
      ready.hidden = false;
      download.disabled = false;
    } catch (problem) {
      showError(problem.message);
    }
  });
  download.addEventListener("click", () => {
    if (pending) saveFile(`${pending.code}.pack`, pending.bytes, "application/octet-stream");
  });
  byId("download-template").addEventListener("click", () => {
    try {
      saveFile("language-template.txt", templateText(), "text/plain;charset=utf-8");
    } catch (problem) {
      showError(problem.message);
    }
  });
  byId("translation-file").addEventListener("change", async event => {
    invalidate();
    const version = ++reading;
    try {
      const file = event.target.files[0];
      const bytes = await readFile(file, 262144);
      if (!bytes || version !== reading) return;
      const text = decode(bytes, "Translation file");
      const translations = parseTranslation(text);
      textInput.value = text;
      if (translations[catalogue().name_id]) nameInput.value = translations[catalogue().name_id];
      const suggested = file.name.replace(/\.txt$/i, "");
      if (!codeInput.value && /^[A-Za-z0-9-]{1,5}$/.test(suggested)) codeInput.value = suggested;
      byId("translation-status").textContent = `Imported ${file.name}. Review the language name and text, then build your pack.`;
    } catch (problem) {
      if (version === reading) showError(problem.message);
    }
  });
  byId("pack-file").addEventListener("change", async event => {
    const result = byId("inspection-result");
    const status = byId("inspection-status");
    const version = ++reading;
    imported = null;
    result.hidden = true;
    byId("edit-pack").disabled = true;
    status.textContent = "";
    try {
      const bytes = await readFile(event.target.files[0], catalogue().pack_limit);
      if (!bytes || version !== reading) return;
      const pack = inspectPack(bytes);
      imported = pack;
      byId("inspection-summary").textContent = `${pack.name} · ${pack.code} · ${pack.size.toLocaleString()} bytes`;
      status.textContent = "This pack matches the current template and the player's supported characters.";
      result.hidden = false;
      byId("edit-pack").disabled = false;
    } catch (problem) {
      if (version === reading) status.textContent = problem.message;
    }
  });
  byId("edit-pack").addEventListener("click", () => {
    if (!imported) return;
    invalidate();
    codeInput.value = imported.code;
    nameInput.value = imported.name;
    textInput.value = catalogue().entries.map(entry => `${entry.id} = ${imported.texts[entry.id].replaceAll("\n", "\\n")}`).join("\n");
    byId("translation-status").textContent = "Pack loaded into the editor. Your original file is unchanged.";
    form.scrollIntoView({behavior: "auto", block: "start"});
    textInput.focus({preventScroll: true});
  });
  try {
    catalogue();
  } catch (problem) {
    form.querySelectorAll("button").forEach(button => { button.disabled = true; });
    showError(problem.message);
  }
})();
