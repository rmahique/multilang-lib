package multilang

// SQLite backend, via database/sql + modernc.org/sqlite (pure Go, no
// CGO) — the zero-setup default, and what the test suite uses.

import (
	"database/sql"
	"time"

	_ "modernc.org/sqlite"
)

const sqliteSchema = `
CREATE TABLE IF NOT EXISTS strings (
    language_id       TEXT NOT NULL,
    string_id         TEXT NOT NULL,
    context           TEXT NOT NULL DEFAULT '',
    content           TEXT NOT NULL,
    original_language TEXT,
    status            TEXT NOT NULL DEFAULT 'draft',
    source_checksum   TEXT,
    updated_by        TEXT,
    date_updated      TEXT NOT NULL,
    PRIMARY KEY (language_id, string_id, context)
)`

const sqliteUpsert = `
INSERT INTO strings
    (language_id, string_id, context, content, original_language,
     status, source_checksum, updated_by, date_updated)
VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?)
ON CONFLICT(language_id, string_id, context) DO UPDATE SET
    content=excluded.content,
    original_language=excluded.original_language,
    status=excluded.status,
    source_checksum=excluded.source_checksum,
    updated_by=excluded.updated_by,
    date_updated=excluded.date_updated`

const sqliteSelect = `
SELECT content FROM strings
WHERE language_id = ? AND string_id = ? AND context = ?`

// SQLiteBackend is the Backend implementation for SQLite.
type SQLiteBackend struct {
	db *sql.DB
}

// NewSQLiteBackend opens a connection to the SQLite file at path (created
// if absent).
func NewSQLiteBackend(path string) (*SQLiteBackend, error) {
	db, err := sql.Open("sqlite", path)
	if err != nil {
		return nil, err
	}
	if _, err := db.Exec("PRAGMA journal_mode = WAL"); err != nil {
		return nil, err
	}
	if _, err := db.Exec("PRAGMA busy_timeout = 60000"); err != nil {
		return nil, err
	}
	if _, err := db.Exec("PRAGMA foreign_keys = ON"); err != nil {
		return nil, err
	}
	return &SQLiteBackend{db: db}, nil
}

// EnsureSchema creates the `strings` table if it doesn't already exist.
func (b *SQLiteBackend) EnsureSchema() error {
	_, err := b.db.Exec(sqliteSchema)
	return err
}

// SelectContent returns content for the given key, and whether a row was found.
func (b *SQLiteBackend) SelectContent(stringID, languageID, context string) (string, bool, error) {
	var content string
	err := b.db.QueryRow(sqliteSelect, languageID, stringID, context).Scan(&content)
	if err == sql.ErrNoRows {
		return "", false, nil
	}
	if err != nil {
		return "", false, err
	}
	return content, true, nil
}

// Upsert inserts row, or updates it in place on primary-key conflict.
func (b *SQLiteBackend) Upsert(row Row) error {
	// SQLite has no native timestamp type; store ISO-8601 text, the same
	// UTC instant every other backend's column ultimately represents.
	_, err := b.db.Exec(
		sqliteUpsert,
		row.LanguageID,
		row.StringID,
		row.Context,
		row.Content,
		nullableString(row.OriginalLanguage),
		row.Status,
		nullableString(row.SourceChecksum),
		nullableString(row.UpdatedBy),
		row.DateUpdated.UTC().Format(time.RFC3339Nano),
	)
	return err
}

// Close closes the underlying connection.
func (b *SQLiteBackend) Close() error {
	return b.db.Close()
}

// DB exposes the raw *sql.DB for tests that need to inspect raw rows.
func (b *SQLiteBackend) DB() *sql.DB {
	return b.db
}

// nullableString turns Go's "" sentinel (see Row) into a real SQL NULL;
// non-empty strings pass through unchanged.
func nullableString(s string) interface{} {
	if s == "" {
		return nil
	}
	return s
}
