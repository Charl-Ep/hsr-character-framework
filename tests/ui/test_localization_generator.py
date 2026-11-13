import importlib.util
import json
from pathlib import Path
import shutil
import tempfile
import unittest

ROOT=Path(__file__).resolve().parents[2]
spec=importlib.util.spec_from_file_location("locale_generator",ROOT/"tools/generate_localization.py")
generator=importlib.util.module_from_spec(spec)
spec.loader.exec_module(generator)


class LocaleGeneratorTests(unittest.TestCase):
    def setUp(self):
        self.temp=tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.source=Path(self.temp.name)/"locales"
        shutil.copytree(ROOT/"locales",self.source)

    def change(self,file,fn):
        path=self.source/file
        value=json.loads(path.read_text(encoding="utf-8"))
        fn(value)
        path.write_text(json.dumps(value,ensure_ascii=False),encoding="utf-8")

    def test_third_language_reorder_repeat_and_fallback(self):
        row={"code":"fr","id":"French","name":"Français","aliases":["fr-FR"],"fonts":["segoeui.ttf"],
             "messages":{"Title":"100% Français", "CreationCount":"{retiring} puis {creations} puis {retiring}"},"source_names":{}}
        (self.source/"fr.json").write_text(json.dumps(row,ensure_ascii=False),encoding="utf-8")
        _,locales,default=generator.load(self.source)
        french=next(row for row in locales if row["code"]=="fr")
        self.assertEqual(french["messages"]["ItemCount"],default["messages"]["ItemCount"])
        output=Path(self.temp.name)/"generated"
        generator.generate(self.source,output)
        self.assertIn("French",(output/"LocaleIds.hpp").read_text(encoding="utf-8"))
        formats=(output/"LocaleFormats.inc").read_text(encoding="utf-8")
        self.assertIn('std::to_string(arg_retiring)+" puis "+std::to_string(arg_creations)+" puis "+std::to_string(arg_retiring)',formats)

    def test_unknown_or_missing_parameters(self):
        for value in ["{wrong}","entries","{count.x}","{count:03d}","{count!r}","{"]:
            with self.subTest(value=value):
                self.change("zh-CN.json",lambda row:row["messages"].update(ItemCount=value))
                with self.assertRaises(ValueError):generator.load(self.source)

    def test_missing_default_rejected(self):
        self.change("en.json",lambda row:row["messages"].pop("Title"))
        with self.assertRaises(ValueError):generator.load(self.source)

    def test_unknown_key_rejected(self):
        self.change("zh-CN.json",lambda row:row["messages"].update(Typo="text"))
        with self.assertRaises(ValueError):generator.load(self.source)

    def test_duplicate_key_rejected(self):
        with self.assertRaises(ValueError):json.loads('{"key":1,"key":2}',object_pairs_hook=generator.unique_object)

    def test_locale_identity_and_widget_identity(self):
        self.change("zh-CN.json",lambda row:row["aliases"].append("EN"))
        with self.assertRaises(ValueError):generator.load(self.source)
        self.change("zh-CN.json",lambda row:row.update(aliases=[],name="中文###collision"))
        with self.assertRaises(ValueError):generator.load(self.source)

    def test_literal_braces_and_percent(self):
        self.assertEqual(generator.tokens("100% {{example}} {count}",{"count":"count"})[-1][1],"count")
        self.assertEqual(generator.display("100% {{example}}",{}),"100% {example}")


if __name__=="__main__":unittest.main()
