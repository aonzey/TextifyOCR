# Minimal shim for Umi-OCR's plugin_i18n module.
# The plugin config modules do `from plugin_i18n import Translator`.
# Umi-OCR's Translator looks up translations in i18n.csv; the CSV keys are
# already the source (Chinese) strings, so returning the key unchanged is a
# correct identity fallback for our use.
class Translator:
    def __init__(self, config_path="", csv_name=""):
        self._path = None
        try:
            import os
            if config_path:
                self._path = os.path.join(os.path.dirname(config_path), csv_name)
        except Exception:
            self._path = None
        self._table = {}
        if self._path:
            try:
                self._load()
            except Exception:
                self._table = {}

    def _load(self):
        # Format: key,en_US,zh_TW,ja_JP  ->  we keep key->key identity, so
        # nothing to parse. Reserved for future use.
        pass

    def __call__(self, text):
        return text
