package multilang

// Connect is the single entry point that turns a backend name +
// credentials into a connected, schema-ready Backend.
//
// Credentials are never accepted as raw SQL fragments and are never
// logged. By default they are read from environment variables so they
// never need to live in source or config files:
//
//	MULTILANG_DB_BACKEND    sqlite | postgres | mysql | filesystem
//	MULTILANG_DB_PATH       (sqlite) file path, (filesystem) root directory
//	MULTILANG_DB_HOST       (postgres/mysql)
//	MULTILANG_DB_PORT       (postgres/mysql)
//	MULTILANG_DB_USER       (postgres/mysql)
//	MULTILANG_DB_PASSWORD   (postgres/mysql)
//	MULTILANG_DB_NAME       (postgres/mysql)
//	MULTILANG_DB_SSLMODE    (postgres/mysql; default "prefer"/"preferred" --
//	                         use TLS if the server offers it; set to
//	                         "require" to make TLS mandatory, "disable" to
//	                         force plaintext)
//
// Explicit fields in Credentials always take precedence over environment
// variables, which is useful for tests and one-off scripts.

import (
	"fmt"
	"os"
	"strconv"
)

var defaultPorts = map[string]int{"postgres": 5432, "mysql": 3306}

// Credentials holds every field any backend might need; only the fields
// relevant to the chosen backend are used. Zero values fall back to the
// matching MULTILANG_DB_* environment variable.
type Credentials struct {
	Path     string // sqlite, filesystem
	Host     string // postgres/mysql
	Port     int    // postgres/mysql
	User     string // postgres/mysql
	Password string // postgres/mysql
	Database string // postgres/mysql
	SSLMode  string // postgres only; "" -> "require"
}

// Connect opens a connection to backend ("sqlite" | "postgres" | "mysql")
// and returns a ready-to-use Backend with its schema ensured.
func Connect(backend string, creds Credentials) (Backend, error) {
	if backend == "" {
		backend = os.Getenv("MULTILANG_DB_BACKEND")
	}
	if backend != "sqlite" && backend != "postgres" && backend != "mysql" && backend != "filesystem" {
		return nil, fmt.Errorf("backend must be one of [sqlite, postgres, mysql, filesystem] — got %q", backend)
	}

	var conn Backend
	var err error

	switch backend {
	case "sqlite":
		path := creds.Path
		if path == "" {
			path = os.Getenv("MULTILANG_DB_PATH")
		}
		if path == "" {
			return nil, fmt.Errorf("sqlite backend requires Path or MULTILANG_DB_PATH")
		}
		conn, err = NewSQLiteBackend(path)

	case "filesystem":
		path := creds.Path
		if path == "" {
			path = os.Getenv("MULTILANG_DB_PATH")
		}
		if path == "" {
			return nil, fmt.Errorf("filesystem backend requires Path or MULTILANG_DB_PATH")
		}
		conn, err = NewFilesystemBackend(path)

	case "postgres", "mysql":
		host := firstNonEmpty(creds.Host, os.Getenv("MULTILANG_DB_HOST"))
		port := creds.Port
		if port == 0 {
			port = envInt("MULTILANG_DB_PORT", defaultPorts[backend])
		}
		user := firstNonEmpty(creds.User, os.Getenv("MULTILANG_DB_USER"))
		password := firstNonEmpty(creds.Password, os.Getenv("MULTILANG_DB_PASSWORD"))
		database := firstNonEmpty(creds.Database, os.Getenv("MULTILANG_DB_NAME"))

		var missing []string
		if host == "" {
			missing = append(missing, "host")
		}
		if user == "" {
			missing = append(missing, "user")
		}
		if password == "" {
			missing = append(missing, "password")
		}
		if database == "" {
			missing = append(missing, "database")
		}
		if len(missing) > 0 {
			return nil, fmt.Errorf("%s backend missing required credentials: %v", backend, missing)
		}

		sslMode := firstNonEmpty(creds.SSLMode, os.Getenv("MULTILANG_DB_SSLMODE"))
		if backend == "postgres" {
			conn, err = NewPostgresBackend(PostgresCredentials{
				Host: host, Port: port, User: user, Password: password,
				Database: database, SSLMode: sslMode,
			})
		} else {
			conn, err = NewMySQLBackend(MySQLCredentials{
				Host: host, Port: port, User: user, Password: password,
				Database: database, SSLMode: sslMode,
			})
		}
	}

	if err != nil {
		return nil, err
	}
	if err := conn.EnsureSchema(); err != nil {
		return nil, err
	}
	return conn, nil
}

func firstNonEmpty(a, b string) string {
	if a != "" {
		return a
	}
	return b
}

func envInt(name string, fallback int) int {
	v := os.Getenv(name)
	if v == "" {
		return fallback
	}
	n, err := strconv.Atoi(v)
	if err != nil {
		return fallback
	}
	return n
}
