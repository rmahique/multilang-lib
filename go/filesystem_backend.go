package multilang

// Filesystem backend — no server, no driver, just files. Useful when the
// translation set is meant to be human-editable and diffable in version
// control rather than queried through a database.
//
// Layout (see ../docs/connectors.md#the-filesystem-backend):
//
//	<root>/<languageID>/<stringID>/<context>/content.json
//	<root>/<languageID>/<stringID>/@default/content.json   (context == "")
//
// The leaf file is always named "content.json" — the row's data never
// becomes part of a filename, only directory names (languageID, stringID,
// context) do.

import (
	"encoding/json"
	"errors"
	"fmt"
	"os"
	"path/filepath"
	"sort"
	"strings"
	"time"
)

const (
	fsDefaultContextDir = "@default"
	fsContentFilename   = "content.json"
)

// FilesystemBackend is the Backend implementation that stores one file
// per (languageID, stringID, context) row under a root directory.
type FilesystemBackend struct {
	root string
}

// NewFilesystemBackend returns a backend rooted at the given directory,
// created on EnsureSchema() if it doesn't already exist.
func NewFilesystemBackend(root string) (*FilesystemBackend, error) {
	abs, err := filepath.Abs(root)
	if err != nil {
		return nil, err
	}
	return &FilesystemBackend{root: abs}, nil
}

// EnsureSchema creates the root directory if it doesn't already exist.
func (b *FilesystemBackend) EnsureSchema() error {
	return os.MkdirAll(b.root, 0o755)
}

// dirFor builds the directory for a row, and refuses to return anything
// outside root. stringID/context allow "." and "-", so a value like ".."
// is a valid identifier (see validation.go) but a directory-traversal
// payload as a path segment. Resolve and check containment before
// touching disk, same spirit as every DB backend parameterizing its
// queries instead of trusting input shape alone.
func (b *FilesystemBackend) dirFor(languageID, stringID, context string) (string, error) {
	contextDir := context
	if context == "" {
		contextDir = fsDefaultContextDir
	}
	dir := filepath.Join(b.root, languageID, stringID, contextDir)
	if dir != b.root && !strings.HasPrefix(dir, b.root+string(filepath.Separator)) {
		return "", fmt.Errorf(
			"refusing to access path outside the filesystem backend root for languageID=%q stringID=%q context=%q",
			languageID, stringID, context,
		)
	}
	return dir, nil
}

type fsRecord struct {
	Content          string  `json:"content"`
	OriginalLanguage *string `json:"original_language"`
	Status           string  `json:"status"`
	SourceChecksum   *string `json:"source_checksum"`
	UpdatedBy        *string `json:"updated_by"`
	DateUpdated      string  `json:"date_updated"`
}

// SelectContent returns content for the given key, and whether a row was found.
func (b *FilesystemBackend) SelectContent(stringID, languageID, context string) (string, bool, error) {
	dir, err := b.dirFor(languageID, stringID, context)
	if err != nil {
		return "", false, err
	}
	data, err := os.ReadFile(filepath.Join(dir, fsContentFilename))
	if errors.Is(err, os.ErrNotExist) {
		return "", false, nil
	}
	if err != nil {
		return "", false, err
	}
	var rec fsRecord
	if err := json.Unmarshal(data, &rec); err != nil {
		return "", false, err
	}
	return rec.Content, true, nil
}

// Upsert writes row to its file, replacing it if it already exists.
//
// Written to a temp file in the same directory and then atomically
// renamed into place (os.Rename, atomic on POSIX for paths on the same
// filesystem) so a concurrent reader never sees a partially written file.
func (b *FilesystemBackend) Upsert(row Row) error {
	dir, err := b.dirFor(row.LanguageID, row.StringID, row.Context)
	if err != nil {
		return err
	}
	if err := os.MkdirAll(dir, 0o755); err != nil {
		return err
	}

	rec := fsRecord{
		Content:          row.Content,
		OriginalLanguage: nullableStringPtr(row.OriginalLanguage),
		Status:           row.Status,
		SourceChecksum:   nullableStringPtr(row.SourceChecksum),
		UpdatedBy:        nullableStringPtr(row.UpdatedBy),
		DateUpdated:      row.DateUpdated.UTC().Format(time.RFC3339Nano),
	}
	data, err := json.MarshalIndent(rec, "", "  ")
	if err != nil {
		return err
	}
	data = append(data, '\n')

	file := filepath.Join(dir, fsContentFilename)
	tmpFile := file + ".tmp"
	if err := os.WriteFile(tmpFile, data, 0o644); err != nil {
		return err
	}
	return os.Rename(tmpFile, file)
}

// SelectRows returns every row matching whichever filters are given, by
// walking the directory tree instead of running a query — there's no
// query engine here, so this is SearchData's only backend-level
// filtering step; the actual content matching happens afterwards,
// in-process, in SearchData itself.
func (b *FilesystemBackend) SelectRows(languageID, status string, context *string) ([]Row, error) {
	var langFilter *string
	if languageID != "" {
		langFilter = &languageID
	}
	var contextDirFilter *string
	if context != nil {
		dir := fsContextDir(*context)
		contextDirFilter = &dir
	}

	langEntries, err := fsListDirs(b.root, langFilter)
	if err != nil {
		return nil, err
	}

	var rows []Row
	for _, lang := range langEntries {
		stringIDEntries, err := fsListDirs(filepath.Join(b.root, lang), nil)
		if err != nil {
			return nil, err
		}
		for _, stringID := range stringIDEntries {
			ctxEntries, err := fsListDirs(filepath.Join(b.root, lang, stringID), contextDirFilter)
			if err != nil {
				return nil, err
			}
			for _, ctxDirName := range ctxEntries {
				path := filepath.Join(b.root, lang, stringID, ctxDirName, fsContentFilename)
				data, err := os.ReadFile(path)
				if errors.Is(err, os.ErrNotExist) {
					continue
				}
				if err != nil {
					return nil, err
				}
				var rec fsRecord
				if err := json.Unmarshal(data, &rec); err != nil {
					return nil, err
				}
				if status != "" && rec.Status != status {
					continue
				}
				ctx := ctxDirName
				if ctx == fsDefaultContextDir {
					ctx = ""
				}
				dateUpdated, err := time.Parse(time.RFC3339Nano, rec.DateUpdated)
				if err != nil {
					return nil, err
				}
				rows = append(rows, Row{
					StringID:         stringID,
					LanguageID:       lang,
					Context:          ctx,
					Content:          rec.Content,
					OriginalLanguage: derefOr(rec.OriginalLanguage, ""),
					Status:           rec.Status,
					SourceChecksum:   derefOr(rec.SourceChecksum, ""),
					UpdatedBy:        derefOr(rec.UpdatedBy, ""),
					DateUpdated:      dateUpdated,
				})
			}
		}
	}
	return rows, nil
}

// fsContextDir maps a context value to its directory name (see the
// layout comment at the top of this file).
func fsContextDir(context string) string {
	if context == "" {
		return fsDefaultContextDir
	}
	return context
}

// fsListDirs returns the subdirectory names of parent — just {*only} if
// only is given and exists, otherwise every subdirectory (sorted, for
// deterministic iteration order). A missing parent yields no entries,
// not an error.
func fsListDirs(parent string, only *string) ([]string, error) {
	if only != nil {
		if info, err := os.Stat(filepath.Join(parent, *only)); err == nil && info.IsDir() {
			return []string{*only}, nil
		}
		return nil, nil
	}
	entries, err := os.ReadDir(parent)
	if errors.Is(err, os.ErrNotExist) {
		return nil, nil
	}
	if err != nil {
		return nil, err
	}
	var names []string
	for _, e := range entries {
		if e.IsDir() {
			names = append(names, e.Name())
		}
	}
	sort.Strings(names)
	return names, nil
}

// derefOr returns *s, or fallback if s is nil — mirrors nullableStringPtr
// (see sqlite_backend.go) for the read direction: a JSON null becomes
// Go's "" sentinel for an unset optional field.
func derefOr(s *string, fallback string) string {
	if s == nil {
		return fallback
	}
	return *s
}

// Close is a no-op — files are opened and closed per call.
func (b *FilesystemBackend) Close() error {
	return nil
}

// nullableStringPtr mirrors nullableString (see sqlite_backend.go) for
// the JSON representation: Go's "" sentinel becomes a real JSON null.
func nullableStringPtr(s string) *string {
	if s == "" {
		return nil
	}
	return &s
}
