# Localization

The interface follows the game's text language. Unsupported or unavailable languages use English. Official character, Path and appearance names remain in the game's selected language.

`locales/messages.json` defines keys, named parameter types and the default language. Each `locales/<tag>.json` supplies UTF-8 translations. CMake generates C++ tables and typed formatters; resources are compiled into the DLL.

## Add a language

```json
{
  "code": "fr",
  "id": "French",
  "name": "Français",
  "aliases": ["fr-FR"],
  "fonts": ["segoeui.ttf"],
  "messages": {
    "Title": "Gestion des personnages HKSR",
    "ItemCount": "Entrées : {count}"
  },
  "source_names": {}
}
```

The filename must match `code`; `id` is a unique C++ identifier and `name` identifies the language. Register the game's text-language tag in `code` or `aliases`, then rebuild.

Tags and aliases are case-insensitive and accept underscores. Register official tags such as `cn` in `aliases`. Unknown tags and missing translations use English; the default language must cover every key.

Duplicate/unknown keys, conflicting tags, incorrect placeholders, NULs and ImGui identity markers fail generation.

## Parameters

Declare parameter types under `messages` in `messages.json`:

```json
{
  "CreationCount": {
    "parameters": {
      "creations": "count",
      "retiring": "count"
    }
  }
}
```

Translations use named placeholders in any order, including repetition. `{{` and `}}` produce literal braces.

```cpp
Text(FormatCreationCount(language, creation_count, retiring_count));
```

`count` maps to `std::uint64_t`; `text` maps to `const std::string&`. Inserted values are literal text. Plural selection and locale-specific number/date formatting are not implemented.

Plain messages use `UiText::Key`. Controls use stable `###Key` identities independent of translated text.

## Names and fonts

`source_names` translates curated fallback keys such as `family:huohuo`. Official names are refreshed when the game's text language changes; resource/action keys and raw errors retain their original text.

Name reads advance in slices and publish one consistent snapshot. A failed field keeps its previous text and reports a localization error; refreshing the source list retries the reads.

`fonts` contains filenames in the Windows fonts directory. Available fonts are merged. The atlas includes compiled text, Chinese and symbols, plus the actual official-name characters in the published catalog; it is rebuilt on the render thread only when its required character ranges change. Supply fonts covering the language's glyphs. Complex shaping and right-to-left layout are not implemented.

Placement/gaze enums map to UI messages; HTTP uses stable state codes. Test resource generation with `framework.locale-resources` and menu rendering with `framework.ui`.
