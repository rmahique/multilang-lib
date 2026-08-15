"""
Filesystem backend — no server, no driver, just files. Useful when the
translation set is meant to be human-editable and diffable in version
control rather than queried through a database.

Layout (see ../../../docs/schema.md for how this maps to the shared schema):

    <root>/<language_id>/<string_id>/<context>/content.json
    <root>/<language_id>/<string_id>/@default/content.json   (context == "")

The leaf file is always named "content.json" — the row's data never
becomes part of a filename, only directory names (language_id, string_id,
context) do.
"""

import json
import os

from .base import Backend

_DEFAULT_CONTEXT_DIR = "@default"
_CONTENT_FILENAME = "content.json"


class FilesystemBackend(Backend):
    """Backend implementation that stores one file per (language_id,
    string_id, context) row under a root directory."""

    def __init__(self, root):
        """
        Args:
            root: Directory the translation tree lives under. Created on
                ensure_schema() if it doesn't already exist.
        """
        self._root = os.path.abspath(root)

    def ensure_schema(self):
        """Create the root directory if it doesn't already exist."""
        os.makedirs(self._root, exist_ok=True)

    def _dir_for(self, language_id, string_id, context):
        context_dir = _DEFAULT_CONTEXT_DIR if context == "" else context
        path = os.path.join(self._root, language_id, string_id, context_dir)
        # string_id/context can't contain "/" (see validation.py), so each
        # is always a single path segment and the fixed 3-level nesting
        # here means a literal ".." can cancel at most one level and still
        # lands back inside root -- not an actual escape. This check is
        # just belt-and-suspenders in case that nesting ever changes.
        resolved = os.path.realpath(path)
        root_resolved = os.path.realpath(self._root)
        if resolved != root_resolved and not resolved.startswith(root_resolved + os.sep):
            raise ValueError(
                "refusing to access path outside the filesystem backend root "
                "for language_id={!r} string_id={!r} context={!r}".format(
                    language_id, string_id, context
                )
            )
        return path

    def select_content(self, string_id, language_id, context):
        """Return content for the given key, or None if no row matches."""
        path = os.path.join(self._dir_for(language_id, string_id, context), _CONTENT_FILENAME)
        try:
            with open(path, encoding="utf-8") as f:
                return json.load(f)["content"]
        except FileNotFoundError:
            return None

    def upsert(self, row):
        """
        Write `row` to its file, replacing it if it already exists.

        Written to a temp file in the same directory and then atomically
        renamed into place (os.replace, atomic on POSIX for paths on the
        same filesystem) so a concurrent reader never sees a partially
        written file.
        """
        directory = self._dir_for(row["language_id"], row["string_id"], row["context"])
        os.makedirs(directory, exist_ok=True)
        record = {
            "content": row["content"],
            "original_language": row["original_language"],
            "status": row["status"],
            "source_checksum": row["source_checksum"],
            "updated_by": row["updated_by"],
            "date_updated": row["date_updated"].isoformat(),
        }
        path = os.path.join(directory, _CONTENT_FILENAME)
        tmp_path = path + ".tmp"
        with open(tmp_path, "w", encoding="utf-8") as f:
            json.dump(record, f, ensure_ascii=False, indent=2)
            f.write("\n")
        os.replace(tmp_path, path)

    def select_rows(self, language_id=None, context=None, status=None):
        """
        Return every row matching whichever filters are not None, by
        walking the directory tree instead of running a query — there's
        no query engine here, so this is search_data's only backend-level
        filtering step; the actual content matching happens afterwards,
        in-process, in search_data itself.
        """
        rows = []
        context_dir_filter = None if context is None else self._context_dir(context)

        for lang_dir, lang in self._iter_dirs(self._root, language_id):
            for string_id_dir, string_id in self._iter_dirs(lang_dir, None):
                for ctx_dir, ctx_dir_name in self._iter_dirs(string_id_dir, context_dir_filter):
                    path = os.path.join(ctx_dir, _CONTENT_FILENAME)
                    try:
                        with open(path, encoding="utf-8") as f:
                            record = json.load(f)
                    except FileNotFoundError:
                        continue
                    if status is not None and record["status"] != status:
                        continue
                    rows.append(
                        {
                            "string_id": string_id,
                            "language_id": lang,
                            "context": "" if ctx_dir_name == _DEFAULT_CONTEXT_DIR else ctx_dir_name,
                            "content": record["content"],
                            "original_language": record["original_language"],
                            "status": record["status"],
                            "source_checksum": record["source_checksum"],
                            "updated_by": record["updated_by"],
                            "date_updated": record["date_updated"],
                        }
                    )
        return rows

    @staticmethod
    def _context_dir(context):
        return _DEFAULT_CONTEXT_DIR if context == "" else context

    @staticmethod
    def _iter_dirs(parent, only_name):
        """
        Yield (path, name) for each subdirectory of `parent` — just
        (parent/only_name, only_name) if only_name is given (skipped
        entirely if it doesn't exist), otherwise every subdirectory.
        Missing/non-existent `parent` yields nothing.
        """
        if only_name is not None:
            path = os.path.join(parent, only_name)
            if os.path.isdir(path):
                yield path, only_name
            return
        try:
            entries = os.listdir(parent)
        except FileNotFoundError:
            return
        for name in sorted(entries):
            path = os.path.join(parent, name)
            if os.path.isdir(path):
                yield path, name

    def close(self):
        """No connection to close — files are opened and closed per call."""
