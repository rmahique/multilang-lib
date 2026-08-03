package multilang

import "time"

// Row is one insert/upsert's worth of data, passed from InsertData to a
// Backend's Upsert method. Optional fields use Go's zero value ("") to
// mean "not set" — OriginalLanguage, SourceChecksum, and UpdatedBy are
// NULL columns; "" is stored as SQL NULL by every backend implementation.
type Row struct {
	StringID         string
	LanguageID       string
	Context          string
	Content          string
	OriginalLanguage string
	Status           string
	SourceChecksum   string
	UpdatedBy        string
	DateUpdated      time.Time
}

// Backend is the common interface every backend (SQLite, PostgreSQL,
// MySQL, filesystem) must implement. All SQL lives here — callers never
// see raw SQL or the raw *sql.DB. Every method must use parameterized
// queries; no value is ever interpolated into a query string. (The
// filesystem backend has no SQL/*sql.DB, but implements the same
// interface shape for the same reason: callers never see its storage
// details either.)
type Backend interface {
	// EnsureSchema creates the `strings` table if it doesn't already exist.
	EnsureSchema() error

	// SelectContent returns content for the given key, and whether a row
	// was found (mirrors the null/None/nil return of the other ports).
	SelectContent(stringID, languageID, context string) (content string, found bool, err error)

	// Upsert inserts row, or updates it in place on primary-key conflict.
	Upsert(row Row) error

	// Close closes the underlying connection.
	Close() error
}
