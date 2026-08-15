'use strict';

/**
 * Filesystem backend — no server, no driver, just files. Useful when the
 * translation set is meant to be human-editable and diffable in version
 * control rather than queried through a database.
 *
 * Layout (see ../../../docs/connectors.md#the-filesystem-backend):
 *
 *   <root>/<languageId>/<stringId>/<context>/content.json
 *   <root>/<languageId>/<stringId>/@default/content.json   (context === '')
 *
 * The leaf file is always named "content.json" — the row's data never
 * becomes part of a filename, only directory names (languageId, stringId,
 * context) do.
 */

const fs = require('fs/promises');
const path = require('path');

const DEFAULT_CONTEXT_DIR = '@default';
const CONTENT_FILENAME = 'content.json';

class FilesystemBackend {
  /** @param {string} root Directory the translation tree lives under. */
  constructor(root) {
    this._root = path.resolve(root);
  }

  /** Create the root directory if it doesn't already exist. */
  async ensureSchema() {
    await fs.mkdir(this._root, { recursive: true });
  }

  /**
   * stringId/context allow "." and "-", so a value like ".." is a valid
   * identifier (see validation.js) but a directory-traversal payload as
   * a path segment. Resolve and check containment before touching disk,
   * same spirit as every DB backend parameterizing its queries instead
   * of trusting input shape alone.
   */
  _dirFor(languageId, stringId, context) {
    const contextDir = context === '' ? DEFAULT_CONTEXT_DIR : context;
    const dir = path.join(this._root, languageId, stringId, contextDir);
    const resolved = path.resolve(dir);
    const rootResolved = path.resolve(this._root);
    if (resolved !== rootResolved && !resolved.startsWith(rootResolved + path.sep)) {
      throw new Error(
        `refusing to access path outside the filesystem backend root for ` +
          `languageId=${JSON.stringify(languageId)} stringId=${JSON.stringify(stringId)} context=${JSON.stringify(context)}`
      );
    }
    return dir;
  }

  /** Return content for the given key, or null if no row matches. */
  async selectContent(stringId, languageId, context) {
    const file = path.join(this._dirFor(languageId, stringId, context), CONTENT_FILENAME);
    try {
      const record = JSON.parse(await fs.readFile(file, 'utf8'));
      return record.content;
    } catch (err) {
      if (err.code === 'ENOENT') {
        return null;
      }
      throw err;
    }
  }

  /**
   * Write `row` to its file, replacing it if it already exists.
   *
   * Written to a temp file in the same directory and then atomically
   * renamed into place (fs.rename, atomic on POSIX for paths on the same
   * filesystem) so a concurrent reader never sees a partially written
   * file.
   */
  async upsert(row) {
    const dir = this._dirFor(row.language_id, row.string_id, row.context);
    await fs.mkdir(dir, { recursive: true });
    const record = {
      content: row.content,
      original_language: row.original_language ?? null,
      status: row.status,
      source_checksum: row.source_checksum ?? null,
      updated_by: row.updated_by ?? null,
      date_updated: row.date_updated.toISOString(),
    };
    const file = path.join(dir, CONTENT_FILENAME);
    const tmpFile = `${file}.tmp`;
    await fs.writeFile(tmpFile, JSON.stringify(record, null, 2) + '\n', 'utf8');
    await fs.rename(tmpFile, file);
  }

  /**
   * Return every row matching whichever of language_id/context/status
   * are non-null, by walking the directory tree instead of running a
   * query — there's no query engine here, so this is searchData's only
   * backend-level filtering step; the actual content matching happens
   * afterwards, in-process, in searchData itself.
   */
  async selectRows({ language_id = null, context = null, status = null } = {}) {
    const contextDirFilter = context === null ? null : context === '' ? DEFAULT_CONTEXT_DIR : context;
    const rows = [];

    const langDirs = await this._listDirs(this._root, language_id);
    for (const lang of langDirs) {
      const stringIdDirs = await this._listDirs(path.join(this._root, lang), null);
      for (const stringId of stringIdDirs) {
        const ctxDirs = await this._listDirs(path.join(this._root, lang, stringId), contextDirFilter);
        for (const ctxDirName of ctxDirs) {
          const file = path.join(this._root, lang, stringId, ctxDirName, CONTENT_FILENAME);
          let record;
          try {
            record = JSON.parse(await fs.readFile(file, 'utf8'));
          } catch (err) {
            if (err.code === 'ENOENT') continue;
            throw err;
          }
          if (status !== null && record.status !== status) continue;
          rows.push({
            string_id: stringId,
            language_id: lang,
            context: ctxDirName === DEFAULT_CONTEXT_DIR ? '' : ctxDirName,
            content: record.content,
            original_language: record.original_language ?? null,
            status: record.status,
            source_checksum: record.source_checksum ?? null,
            updated_by: record.updated_by ?? null,
            date_updated: new Date(record.date_updated),
          });
        }
      }
    }
    return rows;
  }

  /**
   * Return the subdirectory names of `parent` — just [only] if `only` is
   * given and exists, otherwise every subdirectory (sorted, for
   * deterministic iteration order). A missing parent yields no entries,
   * not an error.
   */
  async _listDirs(parent, only) {
    if (only !== null) {
      try {
        const stat = await fs.stat(path.join(parent, only));
        return stat.isDirectory() ? [only] : [];
      } catch (err) {
        if (err.code === 'ENOENT') return [];
        throw err;
      }
    }
    let entries;
    try {
      entries = await fs.readdir(parent, { withFileTypes: true });
    } catch (err) {
      if (err.code === 'ENOENT') return [];
      throw err;
    }
    return entries
      .filter((e) => e.isDirectory())
      .map((e) => e.name)
      .sort();
  }

  /** No connection to close — files are opened and closed per call. */
  async close() {}
}

module.exports = { FilesystemBackend };
