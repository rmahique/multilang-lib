package multilang

// Runs the shared, language-agnostic conformance suite
// (../conformance/cases.json) against this Go implementation.
//
// This is the enforcement mechanism for the "same functionality and
// results across every language and every database" requirement.
//
// By default this runs against SQLite (a fresh temp file per case). Set
// MULTILANG_DB_BACKEND=postgres or =mysql (plus MULTILANG_DB_HOST/_PORT/
// _USER/_PASSWORD/_NAME) to run the exact same suite against a real
// server — see ../conformance/run-live-db-tests.sh, which stands up
// disposable Postgres/MySQL containers and runs every port's suite
// against both.

import (
	"encoding/json"
	"os"
	"path/filepath"
	"testing"
)

var testBackend = envOr("MULTILANG_DB_BACKEND", "sqlite")

func envOr(name, fallback string) string {
	if v := os.Getenv(name); v != "" {
		return v
	}
	return fallback
}

// conformanceConn returns a connection with a guaranteed-empty `strings` table.
// SQLite gets a brand-new temp file per case; Postgres/MySQL share one
// long-lived server across the whole run, so each case truncates the
// table itself instead — cheaper than provisioning a throwaway database
// per case, and just as isolating since every case starts from zero rows
// either way.
func conformanceConn(t *testing.T, name string) Backend {
	t.Helper()
	if testBackend == "sqlite" {
		path := filepath.Join(t.TempDir(), name+".db")
		conn, err := Connect("sqlite", Credentials{Path: path})
		if err != nil {
			t.Fatalf("Connect: %v", err)
		}
		return conn
	}
	if testBackend == "filesystem" {
		path := filepath.Join(t.TempDir(), name)
		conn, err := Connect("filesystem", Credentials{Path: path})
		if err != nil {
			t.Fatalf("Connect: %v", err)
		}
		return conn
	}

	conn, err := Connect(testBackend, Credentials{})
	if err != nil {
		t.Fatalf("Connect(%s): %v", testBackend, err)
	}
	switch b := conn.(type) {
	case *PostgresBackend:
		if _, err := b.DB().Exec("TRUNCATE TABLE strings"); err != nil {
			t.Fatalf("truncate: %v", err)
		}
	case *MySQLBackend:
		if _, err := b.DB().Exec("TRUNCATE TABLE strings"); err != nil {
			t.Fatalf("truncate: %v", err)
		}
	}
	return conn
}

type conformanceOp struct {
	Op          string                 `json:"op"`
	Args        map[string]interface{} `json:"args"`
	Expect      *string                `json:"expect"`
	HasExpect   bool                   `json:"-"`
	ExpectError bool                   `json:"expect_error"`
}

type conformanceCase struct {
	Name       string          `json:"name"`
	Operations []conformanceOp `json:"operations"`
}

type conformanceSuite struct {
	Cases []conformanceCase `json:"cases"`
}

func loadConformanceSuite(t *testing.T) conformanceSuite {
	t.Helper()
	path := filepath.Join("..", "conformance", "cases.json")
	data, err := os.ReadFile(path)
	if err != nil {
		t.Fatalf("reading conformance suite: %v", err)
	}

	// Decode into a raw form first so we can distinguish "expect": null
	// (row not found) from "expect" being absent (no assertion).
	var raw struct {
		Cases []struct {
			Name       string `json:"name"`
			Operations []struct {
				Op          string                 `json:"op"`
				Args        map[string]interface{} `json:"args"`
				Expect      json.RawMessage        `json:"expect"`
				ExpectError bool                   `json:"expect_error"`
			} `json:"operations"`
		} `json:"cases"`
	}
	if err := json.Unmarshal(data, &raw); err != nil {
		t.Fatalf("parsing conformance suite: %v", err)
	}

	var suite conformanceSuite
	for _, rc := range raw.Cases {
		c := conformanceCase{Name: rc.Name}
		for _, ro := range rc.Operations {
			op := conformanceOp{Op: ro.Op, Args: ro.Args, ExpectError: ro.ExpectError}
			if ro.Expect != nil {
				op.HasExpect = true
				var s *string
				_ = json.Unmarshal(ro.Expect, &s) // null -> nil, string -> pointer
				op.Expect = s
			}
			c.Operations = append(c.Operations, op)
		}
		suite.Cases = append(suite.Cases, c)
	}
	return suite
}

func argString(args map[string]interface{}, key string) string {
	v, ok := args[key]
	if !ok || v == nil {
		return ""
	}
	return v.(string)
}

func TestConformance(t *testing.T) {
	suite := loadConformanceSuite(t)

	for _, c := range suite.Cases {
		c := c
		t.Run(c.Name, func(t *testing.T) {
			conn := conformanceConn(t, c.Name)
			defer conn.Close()

			for _, step := range c.Operations {
				args := step.Args
				var opErr error
				var result string
				var found bool

				if step.Op == "retrieve_data" {
					result, found, opErr = RetrieveData(
						conn, argString(args, "string_id"), argString(args, "language_id"), argString(args, "context"),
					)
				} else {
					opErr = InsertData(
						conn, argString(args, "string_id"), argString(args, "language_id"), argString(args, "content"),
						InsertOptions{
							Context:          argString(args, "context"),
							OriginalLanguage: argString(args, "original_language"),
							Status:           argString(args, "status"),
							UpdatedBy:        argString(args, "updated_by"),
						},
					)
				}

				if step.ExpectError {
					if _, ok := opErr.(*ValidationError); !ok {
						t.Fatalf("op %s: expected ValidationError, got %v", step.Op, opErr)
					}
					continue
				}
				if opErr != nil {
					t.Fatalf("op %s: unexpected error: %v", step.Op, opErr)
				}
				if step.HasExpect {
					if step.Expect == nil {
						if found {
							t.Fatalf("op %s: expected no row, got %q", step.Op, result)
						}
					} else {
						if !found || result != *step.Expect {
							t.Fatalf("op %s: got (%q, found=%v), want %q", step.Op, result, found, *step.Expect)
						}
					}
				}
			}
		})
	}
}
