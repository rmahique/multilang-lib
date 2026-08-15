package multilang

// MySQL/MariaDB backend, via database/sql + go-sql-driver/mysql.
//
// Requires MySQL 5.7.9+ or MariaDB 10.2.2+ (innodb_large_prefix on by
// default, DYNAMIC row format default) — the composite primary key
// (language_id, string_id, context) in utf8mb4 can exceed the legacy
// 767-byte InnoDB index-prefix limit on older versions/configurations.

import (
	"database/sql"
	"fmt"

	_ "github.com/go-sql-driver/mysql"
)

const mysqlSchema = `
CREATE TABLE IF NOT EXISTS strings (
    language_id       VARCHAR(35) NOT NULL,
    string_id         VARCHAR(200) NOT NULL,
    context           VARCHAR(200) NOT NULL DEFAULT '',
    content           MEDIUMTEXT NOT NULL,
    original_language VARCHAR(35),
    status            VARCHAR(20) NOT NULL DEFAULT 'draft',
    source_checksum   VARCHAR(64),
    updated_by        VARCHAR(200),
    date_updated      DATETIME NOT NULL,
    PRIMARY KEY (language_id, string_id, context)
) ENGINE=InnoDB ROW_FORMAT=DYNAMIC DEFAULT CHARSET=utf8mb4`

const mysqlUpsert = `
INSERT INTO strings
    (language_id, string_id, context, content, original_language,
     status, source_checksum, updated_by, date_updated)
VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?)
ON DUPLICATE KEY UPDATE
    content=VALUES(content),
    original_language=VALUES(original_language),
    status=VALUES(status),
    source_checksum=VALUES(source_checksum),
    updated_by=VALUES(updated_by),
    date_updated=VALUES(date_updated)`

const mysqlSelect = `
SELECT content FROM strings
WHERE language_id = ? AND string_id = ? AND context = ?`

const mysqlSelectRowsBase = `
SELECT string_id, language_id, context, content, original_language,
       status, source_checksum, updated_by, date_updated
FROM strings`

// MySQLBackend is the Backend implementation for MySQL/MariaDB.
type MySQLBackend struct {
	db *sql.DB
}

// MySQLCredentials holds connection parameters for NewMySQLBackend.
// SSLMode mirrors PostgresCredentials.SSLMode: "" defaults to
// "preferred" (use TLS if the server offers it, degrade to plaintext if
// it doesn't -- go-sql-driver/mysql supports this natively, unlike most
// MySQL client libraries). "require" encrypts without verifying the
// server's certificate (skip-verify) -- matching the same non-verifying
// "require" semantics the Postgres backends use, appropriate for a
// self-signed cert on a container/localhost server. "disable" forces
// plaintext.
type MySQLCredentials struct {
	Host     string
	Port     int
	User     string
	Password string
	Database string
	SSLMode  string
}

// NewMySQLBackend opens a connection using the given credentials.
// Credentials are passed as driver connection parameters, never
// interpolated into a query string or logged.
func NewMySQLBackend(c MySQLCredentials) (*MySQLBackend, error) {
	tls := "preferred"
	switch c.SSLMode {
	case "", "prefer", "preferred":
		tls = "preferred"
	case "disable":
		tls = "false"
	case "require", "skip-verify":
		tls = "skip-verify"
	case "verify-full", "true":
		tls = "true"
	default:
		tls = c.SSLMode // pass through a custom registered tls config name
	}

	dsn := fmt.Sprintf(
		"%s:%s@tcp(%s:%d)/%s?charset=utf8mb4&parseTime=true&tls=%s",
		c.User, c.Password, c.Host, c.Port, c.Database, tls,
	)
	db, err := sql.Open("mysql", dsn)
	if err != nil {
		return nil, err
	}
	return &MySQLBackend{db: db}, nil
}

// EnsureSchema creates the `strings` table if it doesn't already exist.
func (b *MySQLBackend) EnsureSchema() error {
	_, err := b.db.Exec(mysqlSchema)
	return err
}

// SelectContent returns content for the given key, and whether a row was found.
func (b *MySQLBackend) SelectContent(stringID, languageID, context string) (string, bool, error) {
	var content string
	err := b.db.QueryRow(mysqlSelect, languageID, stringID, context).Scan(&content)
	if err == sql.ErrNoRows {
		return "", false, nil
	}
	if err != nil {
		return "", false, err
	}
	return content, true, nil
}

// Upsert inserts row, or updates it in place on primary-key conflict.
func (b *MySQLBackend) Upsert(row Row) error {
	// go-sql-driver/mysql formats a time.Time into MySQL's DATETIME
	// literal correctly (with parseTime=true above); no manual
	// formatting needed, unlike a hand-built ISO-8601 string.
	_, err := b.db.Exec(
		mysqlUpsert,
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
func (b *MySQLBackend) SelectRows(languageID, status string, context *string) ([]Row, error) {
	where, args := searchWhereClause(languageID, status, context, questionMarkPlaceholder)
	query := mysqlSelectRowsBase
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
func (b *MySQLBackend) Close() error {
	return b.db.Close()
}

// DB exposes the raw *sql.DB for tests that need to inspect/reset raw rows.
func (b *MySQLBackend) DB() *sql.DB {
	return b.db
}
