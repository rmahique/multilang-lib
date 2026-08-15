package multilang

// PostgreSQL backend, via database/sql + lib/pq.

import (
	"database/sql"
	"fmt"

	_ "github.com/lib/pq"
)

const postgresSchema = `
CREATE TABLE IF NOT EXISTS strings (
    language_id       TEXT NOT NULL,
    string_id         TEXT NOT NULL,
    context           TEXT NOT NULL DEFAULT '',
    content           TEXT NOT NULL,
    original_language TEXT,
    status            TEXT NOT NULL DEFAULT 'draft',
    source_checksum   TEXT,
    updated_by        TEXT,
    date_updated      TIMESTAMPTZ NOT NULL,
    PRIMARY KEY (language_id, string_id, context)
)`

const postgresUpsert = `
INSERT INTO strings
    (language_id, string_id, context, content, original_language,
     status, source_checksum, updated_by, date_updated)
VALUES ($1, $2, $3, $4, $5, $6, $7, $8, $9)
ON CONFLICT (language_id, string_id, context) DO UPDATE SET
    content=excluded.content,
    original_language=excluded.original_language,
    status=excluded.status,
    source_checksum=excluded.source_checksum,
    updated_by=excluded.updated_by,
    date_updated=excluded.date_updated`

const postgresSelect = `
SELECT content FROM strings
WHERE language_id = $1 AND string_id = $2 AND context = $3`

const postgresSelectRowsBase = `
SELECT string_id, language_id, context, content, original_language,
       status, source_checksum, updated_by, date_updated
FROM strings`

// PostgresBackend is the Backend implementation for PostgreSQL.
type PostgresBackend struct {
	db *sql.DB
}

// PostgresCredentials holds connection parameters for NewPostgresBackend.
// SSLMode defaults to "" being treated as "prefer" by the caller
// (Connect) — use TLS if the server offers it, but don't fail the
// connection if it doesn't. A plain localhost/container Postgres has no
// certificate to negotiate, and requiring TLS is an opt-in the caller
// makes explicitly (SSLMode: "require", or MULTILANG_DB_SSLMODE), not
// something forced on every connection.
type PostgresCredentials struct {
	Host     string
	Port     int
	User     string
	Password string
	Database string
	SSLMode  string
}

// NewPostgresBackend opens a connection using the given credentials.
// Credentials are passed as driver connection parameters, never
// interpolated into a query string or logged.
func NewPostgresBackend(c PostgresCredentials) (*PostgresBackend, error) {
	sslMode := c.SSLMode
	if sslMode == "" {
		sslMode = "prefer"
	}
	dsn := fmt.Sprintf(
		"host=%s port=%d user=%s password=%s dbname=%s sslmode=%s",
		c.Host, c.Port, c.User, c.Password, c.Database, sslMode,
	)
	db, err := sql.Open("postgres", dsn)
	if err != nil {
		return nil, err
	}
	return &PostgresBackend{db: db}, nil
}

// EnsureSchema creates the `strings` table if it doesn't already exist.
func (b *PostgresBackend) EnsureSchema() error {
	_, err := b.db.Exec(postgresSchema)
	return err
}

// SelectContent returns content for the given key, and whether a row was found.
func (b *PostgresBackend) SelectContent(stringID, languageID, context string) (string, bool, error) {
	var content string
	err := b.db.QueryRow(postgresSelect, languageID, stringID, context).Scan(&content)
	if err == sql.ErrNoRows {
		return "", false, nil
	}
	if err != nil {
		return "", false, err
	}
	return content, true, nil
}

// Upsert inserts row, or updates it in place on primary-key conflict.
func (b *PostgresBackend) Upsert(row Row) error {
	_, err := b.db.Exec(
		postgresUpsert,
		row.LanguageID,
		row.StringID,
		row.Context,
		row.Content,
		nullableString(row.OriginalLanguage),
		row.Status,
		nullableString(row.SourceChecksum),
		nullableString(row.UpdatedBy),
		row.DateUpdated,
	)
	return err
}

// SelectRows returns every row matching whichever filters are given.
func (b *PostgresBackend) SelectRows(languageID, status string, context *string) ([]Row, error) {
	where, args := searchWhereClause(languageID, status, context, dollarPlaceholder)
	query := postgresSelectRowsBase
	if where != "" {
		query += " WHERE " + where
	}

	rows, err := b.db.Query(query, args...)
	if err != nil {
		return nil, err
	}
	defer rows.Close()

	var result []Row
	for rows.Next() {
		var r Row
		var originalLanguage, sourceChecksum, updatedBy sql.NullString
		if err := rows.Scan(&r.StringID, &r.LanguageID, &r.Context, &r.Content,
			&originalLanguage, &r.Status, &sourceChecksum, &updatedBy, &r.DateUpdated); err != nil {
			return nil, err
		}
		r.OriginalLanguage = originalLanguage.String
		r.SourceChecksum = sourceChecksum.String
		r.UpdatedBy = updatedBy.String
		result = append(result, r)
	}
	return result, rows.Err()
}

// Close closes the underlying connection.
func (b *PostgresBackend) Close() error {
	return b.db.Close()
}

// DB exposes the raw *sql.DB for tests that need to inspect/reset raw rows.
func (b *PostgresBackend) DB() *sql.DB {
	return b.db
}
