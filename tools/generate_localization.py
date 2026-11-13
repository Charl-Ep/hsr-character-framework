"""Compile reviewed UTF-8 locale documents to immutable, typed C++ resources."""
import argparse
import json
from pathlib import Path
import re
import string


def unique_object(pairs):
    result = {}
    for key, value in pairs:
        if key in result:
            raise ValueError(f"duplicate JSON key: {key}")
        result[key] = value
    return result


def read(path):
    return json.loads(path.read_text(encoding="utf-8"), object_pairs_hook=unique_object)


def fields(value, allowed):
    if not isinstance(value, dict) or set(value) - set(allowed):
        raise ValueError(f"unexpected fields; expected {allowed}")


def text(value):
    if not isinstance(value, str) or not value or "\0" in value or "##" in value:
        raise ValueError("text must be nonempty, without NUL or ImGui ID markers")
    return value


def identifier(value):
    if not isinstance(value, str) or not re.fullmatch(r"[A-Za-z][A-Za-z0-9_]*", value) or value in {"Count", "language"}:
        raise ValueError(f"invalid C++ resource identifier: {value}")
    return value


def tokens(value, parameters):
    result = list(string.Formatter().parse(text(value)))
    names = []
    for _, name, spec, conversion in result:
        if name is not None:
            if name not in parameters or spec or conversion:
                raise ValueError(f"unknown/unsupported placeholder: {name}")
            names.append(name)
    if set(names) != set(parameters):
        raise ValueError(f"placeholder mismatch: expected {list(parameters)}, got {names}")
    return result


def load(directory):
    schema = read(directory / "messages.json")
    fields(schema, {"default_language", "messages"})
    messages = schema["messages"]
    if not isinstance(messages, dict) or not messages:
        raise ValueError("messages must be a nonempty object")
    for key, row in messages.items():
        identifier(key)
        fields(row, {"parameters"})
        params = row["parameters"]
        if not isinstance(params, dict):
            raise ValueError("parameters must be an object")
        for name, kind in params.items():
            identifier(name)
            if kind not in {"text", "count"}:
                raise ValueError(f"unknown argument type: {kind}")
    locales = []
    codes, ids = set(), set()
    for path in sorted(directory.glob("*.json")):
        if path.name == "messages.json":
            continue
        row = read(path)
        fields(row, {"code", "id", "name", "aliases", "fonts", "messages", "source_names"})
        if not isinstance(row["code"], str) or path.stem != row["code"]:
            raise ValueError(f"locale filename must match code: {path.name}")
        identifier(row["id"])
        if row["id"] in ids:
            raise ValueError("duplicate locale identifier")
        ids.add(row["id"])
        if not isinstance(row["aliases"], list) or not isinstance(row["fonts"], list):
            raise ValueError("aliases and fonts must be arrays")
        for code in [row["code"]] + row["aliases"]:
            if not isinstance(code, str) or not re.fullmatch(r"[A-Za-z0-9]+(?:-[A-Za-z0-9]+)*", code) or code.lower() in codes:
                raise ValueError(f"duplicate/invalid language tag: {code}")
            codes.add(code.lower())
        for font in row["fonts"]:
            if not isinstance(font, str) or not re.fullmatch(r"[A-Za-z0-9_.-]+\.(?:ttf|ttc|otf)", font):
                raise ValueError("fonts must be filenames in the Windows fonts directory")
        text(row["name"])
        if not isinstance(row["messages"], dict) or set(row["messages"]) - set(messages):
            raise ValueError(f"unknown message key in {path.name}")
        for key, value in row["messages"].items():
            tokens(value, messages[key]["parameters"])
        if not isinstance(row["source_names"], dict):
            raise ValueError("source_names must be an object")
        for key, value in row["source_names"].items():
            text(key); text(value)
        locales.append(row)
    defaults = [row for row in locales if row["code"] == schema["default_language"]]
    if len(defaults) != 1 or set(defaults[0]["messages"]) != set(messages):
        raise ValueError("default language must exist and translate every message")
    default = defaults[0]
    for row in locales:
        row["missing"] = sorted(set(messages) - set(row["messages"]))
        row["messages"] = {**default["messages"], **row["messages"]}
        row["source_names"] = {**default["source_names"], **row["source_names"]}
    return schema, locales, default


def literal(value):
    return json.dumps(value, ensure_ascii=False)


def display(value, parameters):
    # Unescape literal braces for messages with no runtime arguments, too.
    return value if parameters else "".join(part for part, _, _, _ in tokens(value, parameters))


def generate(directory, output):
    schema, locales, default = load(directory)
    messages = schema["messages"]
    header = ["// Generated; edit locales/*.json, not this file.", "#pragma once", "#include <cstddef>", "#include <cstdint>", "#include <string>",
              "namespace hksr::framework::ui {", "enum class Language {" + ",".join(row["id"] for row in locales) + ",Count};",
              "enum class UiText {" + ",".join(messages) + ",Count};",
              f"inline constexpr Language DefaultLocale=Language::{default['id']};",
              f"inline constexpr std::size_t LocaleCount={len(locales)};"]
    data = ["// Generated immutable data; no runtime JSON dependency.", "constexpr TextRow entries[]={"]
    for key in messages:
        data.append("{" + literal(key) + ",{" + ",".join(literal(display(row["messages"][key],messages[key]["parameters"])) for row in locales) + "}},")
    data += ["};", "const std::vector<LanguageInfo> languages={"]
    for row in locales:
        data.append("{Language::" + row["id"] + "," + literal(row["code"]) + "," + literal(row["name"]) + "},")
    data += ["};", "constexpr Alias aliases[]={"]
    for row in locales:
        for alias in [row["code"]] + row["aliases"]:
            data.append("{" + literal(alias.lower()) + ",Language::" + row["id"] + "},")
    data += ["};", "const std::vector<const char*> font_files={"]
    for font in dict.fromkeys(font for row in locales for font in row["fonts"]):
        data.append(literal(font) + ",")
    data += ["};", "const std::vector<TextRow> source_names={"]
    for key in sorted({key for row in locales for key in row["source_names"]}):
        data.append("{" + literal(key) + ",{" + ",".join(literal(row["source_names"].get(key, "")) for row in locales) + "}},")
    data += ["};"]
    glyphs = "".join(sorted({row["name"] for row in locales} | {value for row in locales for value in row["messages"].values()} | {value for row in locales for value in row["source_names"].values()}))
    # Adjacent literals avoid compiler string-literal length limits.
    data.append("constexpr const char* glyph_chunks[]={")
    data += [literal(glyphs[i:i+512])+"," for i in range(0,len(glyphs),512)] + ["};"]
    formatters = ["// Generated typed, named-parameter formatters."]
    for key, spec in messages.items():
        params = spec["parameters"]
        if not params:
            continue
        declaration = "std::string Format" + key + "(Language language" + "".join(
            ", " + ("const std::string& " if kind == "text" else "std::uint64_t ") + "arg_" + name for name,kind in params.items()) + ")"
        header.append(declaration + ";")
        formatters += [declaration + " {", "switch(language) {"]
        for row in locales:
            expression = ["std::string{}"]
            for part, name, _, _ in tokens(row["messages"][key], params):
                if part: expression.append(literal(part))
                if name is not None: expression.append("arg_" + name if params[name] == "text" else "std::to_string(arg_" + name + ")")
            formatters.append("case Language::" + row["id"] + ":return " + "+".join(expression) + ";")
        arguments = ",".join("arg_" + name for name in params)
        formatters += ["default:return Format" + key + "(DefaultLocale," + arguments + ");", "}", "}"]
    header.append("}")
    output.mkdir(parents=True,exist_ok=True)
    for name, lines in [("LocaleIds.hpp",header),("LocaleData.inc",data),("LocaleFormats.inc",formatters)]:
        (output/name).write_text("\n".join(lines)+"\n",encoding="utf-8")
    for row in locales:
        if row["missing"]:
            print(f"{row['code']}: {len(row['missing'])} missing messages use {default['code']}")


if __name__ == "__main__":
    parser=argparse.ArgumentParser()
    parser.add_argument("--source",type=Path,required=True)
    parser.add_argument("--output",type=Path,required=True)
    args=parser.parse_args()
    generate(args.source,args.output)
