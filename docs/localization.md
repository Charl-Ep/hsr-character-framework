# Localization

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

The filename must match `code`; `id` is a unique C++ identifier and `name` is the menu label. Rebuild to register the language.

Tags and aliases are case-insensitive and accept underscores. Resolution falls back through parent tags, then the caller's fallback. Settings save the canonical tag. Missing translations use `en`; the default language must cover every key.

Duplicate/unknown keys, conflicting tags, incorrect placeholders, NULs and ImGui identity markers fail generation.

## Parameters

Declare parameter types in `messages.json`:

```json
"CreationCount": {
  "parameters": {
    "creations": "count",
    "retiring": "count"
  }
}
```

Translations use `{creations}` and `{retiring}` in any order. Repetition is allowed; `{{` and `}}` are literal braces.

```cpp
Text(FormatCreationCount(language, creation_count, retiring_count));
```

`count` maps to `std::uint64_t`; `text` maps to `const std::string&`. Inserted values are not parsed again. Plural selection and locale-specific number/date formatting are not implemented.

Plain messages use `UiText::Key`. Controls use stable `###Key` identities independent of translated text.

## Names and fonts

`source_names` translates curated keys such as `family:huohuo`; search includes all registered translations. Official resource/action keys and raw errors retain their original text.

`fonts` contains filenames in the Windows fonts directory. Available fonts are merged, with glyph ranges from compiled text and catalog characters. Supply fonts covering the language's glyphs. Complex shaping and right-to-left layout are not implemented.

Placement/gaze enums map to UI messages; HTTP uses stable state codes. Test resource generation with `framework.locale-resources` and menu rendering with `framework.ui`.
