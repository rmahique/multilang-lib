package multilang

import (
	"fmt"
	"strings"
	"time"
)

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

	// SelectRows returns every row matching whichever filters are given:
	// languageID/status ("" = no filter), context (nil = no filter; a
	// pointer to "" filters for only the default/un-contextualized row).
	// No content matching happens here -- SearchData does its own
	// in-process regex/natural/exact matching over whatever this
	// returns, which is what keeps search behavior identical across
	// every backend (see docs/search.md).
	SelectRows(languageID, status string, context *string) ([]Row, error)

	// Close closes the underlying connection.
	Close() error
}

// searchWhereClause builds a parameterized WHERE clause (without the
// WHERE keyword) for whichever of languageID/status/context filters are
// given, using placeholderFor(n) to generate the nth placeholder in
// whatever a backend's driver expects ("?" for SQLite/MySQL, "$1"... for
// Postgres). Returns the clause text ("" if no filters were given) and
// the bind args in the same order. Shared across the three SQL backends'
// SelectRows so the filter-building logic can't drift between them.
func searchWhereClause(languageID, status string, context *string, placeholderFor func(n int) string) (string, []interface{}) {
	var clauses []string
	var args []interface{}
	n := 0
	add := func(column, value string) {
		n++
		clauses = append(clauses, column+" = "+placeholderFor(n))
		args = append(args, value)
	}
	if languageID != "" {
		add("language_id", languageID)
	}
	if context != nil {
		add("context", *context)
	}
	if status != "" {
		add("status", status)
	}
	return strings.Join(clauses, " AND "), args
}

func questionMarkPlaceholder(_ int) string { return "?" }

func dollarPlaceholder(n int) string { return fmt.Sprintf("$%d", n) }
