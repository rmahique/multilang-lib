# Go

## Install

Add it as a dependency of your module:

```bash
go get github.com/rmahique/multilang-lib/go
```

Uses the pure-Go `modernc.org/sqlite` driver for SQLite — no CGO
required.

## The example

Insert a source string plus a translation, retrieve with context
disambiguation, handle `*ValidationError`, and switch backends via
environment variables. Copied verbatim from
[`go/examples/basic_usage.go`](https://github.com/rmahique/multilang-lib/blob/main/go/examples/basic_usage.go).

```go
package main

import (
	"fmt"
	"os"
	"path/filepath"

	"github.com/rmahique/multilang-lib/go"
)

func main() {
	// multilang.Connect reads MULTILANG_DB_BACKEND (and the matching
	// MULTILANG_DB_HOST/_USER/_PASSWORD/_NAME/_PORT) if set; falling
	// back to a temp SQLite file here just keeps this example runnable
	// with no setup at all.
	backend := os.Getenv("MULTILANG_DB_BACKEND")
	if backend == "" {
		backend = "sqlite"
	}

	var creds multilang.Credentials
	if backend == "sqlite" && os.Getenv("MULTILANG_DB_PATH") == "" {
		creds.Path = filepath.Join(os.TempDir(), "multilang-example.db")
	}

	conn, err := multilang.Connect(backend, creds)
	if err != nil {
		panic(err)
	}
	defer conn.Close()

	fmt.Printf("Connected via backend=%q\n", backend)

	// --- Insert a source string, then a translation of it -------------
	must(multilang.InsertData(conn, "greeting", "en", "Hello world", multilang.InsertOptions{}))
	must(multilang.InsertData(conn, "greeting", "es", "Hola mundo", multilang.InsertOptions{
		OriginalLanguage: "en",
	}))
	// OriginalLanguage: "en" makes InsertData hash the current English
	// content and store that hash as source_checksum -- the basis for
	// detecting later that a translation has gone stale relative to its
	// source. RetrieveData itself never returns that metadata (data
	// only, by design).

	content, found, err := multilang.RetrieveData(conn, "greeting", "es", "")
	must(err)
	fmt.Println(content, found) // -> "Hola mundo" true

	// --- context disambiguates the same string_id used two ways -------
	must(multilang.InsertData(conn, "post", "en", "Publish", multilang.InsertOptions{Context: "button.publish"}))
	must(multilang.InsertData(conn, "post", "en", "Post", multilang.InsertOptions{Context: "menu.item"}))

	c1, _, err := multilang.RetrieveData(conn, "post", "en", "button.publish")
	must(err)
	c2, _, err := multilang.RetrieveData(conn, "post", "en", "menu.item")
	must(err)
	fmt.Println(c1) // -> "Publish"
	fmt.Println(c2) // -> "Post"

	// --- SearchData: find rows by content, not by exact key -----------
	must(multilang.InsertData(conn, "welcome1", "en", "Welcome to our platform", multilang.InsertOptions{}))
	must(multilang.InsertData(conn, "welcome2", "en", "Welcome back, friend", multilang.InsertOptions{}))
	matches, err := multilang.SearchData(conn, "welcome", multilang.SearchModeNatural, multilang.SearchOptions{LanguageID: "en"})
	must(err)
	for _, r := range matches {
		fmt.Println(r.StringID, "->", r.Content)
	}
	// -> welcome1 -> Welcome to our platform
	// -> welcome2 -> Welcome back, friend

	// --- RetrieveData on a row that doesn't exist: found=false, not an error
	_, found, err = multilang.RetrieveData(conn, "greeting", "fr", "")
	must(err)
	fmt.Println("found:", found) // -> false

	// --- invalid input returns *ValidationError, not some generic error
	err = multilang.InsertData(conn, "greeting", "not-a-valid-bcp47-tag!!", "test", multilang.InsertOptions{})
	if verr, ok := err.(*multilang.ValidationError); ok {
		fmt.Println("rejected as expected:", verr.Error())
	} else if err != nil {
		panic(err)
	}
}

func must(err error) {
	if err != nil {
		panic(err)
	}
}
```

## Run it

```bash
cd go
go run ./examples
```

Point it at a real Postgres/MySQL server, or the filesystem backend,
with no code changes — see [Switching backends](index.md#switching-backends).

## Distro packages

Debian/Ubuntu, RHEL/Fedora, and openSUSE/SLES packaging (source-only,
GOPATH-style) lives in `go/packaging/` — see that directory's
`README.md`, or grab a prebuilt one from
[GitHub Releases](https://github.com/rmahique/multilang-lib/releases/latest).
