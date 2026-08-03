package multilang

import (
	"path/filepath"
	"testing"
)

func freshConn(t *testing.T, name string) *SQLiteBackend {
	t.Helper()
	path := filepath.Join(t.TempDir(), name+".db")
	conn, err := Connect("sqlite", Credentials{Path: path})
	if err != nil {
		t.Fatalf("Connect: %v", err)
	}
	sb := conn.(*SQLiteBackend)
	t.Cleanup(func() { sb.Close() })
	return sb
}

func TestInsertThenRetrieve(t *testing.T) {
	conn := freshConn(t, "t1")
	if err := InsertData(conn, "greeting", "en", "Hello world", InsertOptions{}); err != nil {
		t.Fatal(err)
	}
	content, found, err := RetrieveData(conn, "greeting", "en", "")
	if err != nil || !found || content != "Hello world" {
		t.Errorf("got (%q, %v, %v), want (\"Hello world\", true, nil)", content, found, err)
	}
}

func TestMissingRowReturnsNotFound(t *testing.T) {
	conn := freshConn(t, "t2")
	_, found, err := RetrieveData(conn, "nope", "en", "")
	if err != nil || found {
		t.Errorf("got found=%v err=%v, want found=false", found, err)
	}
}

func TestUpsertUpdatesExistingRow(t *testing.T) {
	conn := freshConn(t, "t3")
	_ = InsertData(conn, "greeting", "en", "Hello", InsertOptions{})
	_ = InsertData(conn, "greeting", "en", "Hello!", InsertOptions{})
	content, _, _ := RetrieveData(conn, "greeting", "en", "")
	if content != "Hello!" {
		t.Errorf("got %q, want \"Hello!\"", content)
	}
}

func TestDifferentlyCasedLanguageIDIsSameRow(t *testing.T) {
	conn := freshConn(t, "t4")
	_ = InsertData(conn, "greeting", "en-US", "Hello", InsertOptions{})
	_ = InsertData(conn, "greeting", "en-us", "Hello there", InsertOptions{})
	content, _, _ := RetrieveData(conn, "greeting", "EN-US", "")
	if content != "Hello there" {
		t.Errorf("got %q, want \"Hello there\"", content)
	}
}

func TestDifferentlyCasedStringIDIsSameRow(t *testing.T) {
	conn := freshConn(t, "t5")
	_ = InsertData(conn, "Greeting", "en", "Hello", InsertOptions{})
	_ = InsertData(conn, "GREETING", "en", "Hello there", InsertOptions{})
	content, _, _ := RetrieveData(conn, "greeting", "en", "")
	if content != "Hello there" {
		t.Errorf("got %q, want \"Hello there\"", content)
	}
}

func TestDifferentlyCasedContextIsSameRow(t *testing.T) {
	conn := freshConn(t, "t6")
	_ = InsertData(conn, "post", "fr", "Publier", InsertOptions{Context: "Button.Publish"})
	_ = InsertData(conn, "post", "fr", "Publier!", InsertOptions{Context: "button.publish"})
	content, _, _ := RetrieveData(conn, "post", "fr", "BUTTON.PUBLISH")
	if content != "Publier!" {
		t.Errorf("got %q, want \"Publier!\"", content)
	}
}

func TestContextDisambiguatesSameStringID(t *testing.T) {
	conn := freshConn(t, "t7")
	_ = InsertData(conn, "post", "fr", "Publier", InsertOptions{Context: "button.publish"})
	_ = InsertData(conn, "post", "fr", "Article", InsertOptions{Context: "menu.item"})

	c1, _, _ := RetrieveData(conn, "post", "fr", "button.publish")
	c2, _, _ := RetrieveData(conn, "post", "fr", "menu.item")
	if c1 != "Publier" || c2 != "Article" {
		t.Errorf("got (%q, %q), want (\"Publier\", \"Article\")", c1, c2)
	}
}

func TestTranslationComputesSourceChecksum(t *testing.T) {
	conn := freshConn(t, "t8")
	_ = InsertData(conn, "greeting", "en", "Hello world", InsertOptions{})
	_ = InsertData(conn, "greeting", "es", "Hola mundo", InsertOptions{OriginalLanguage: "en"})

	var checksum, originalLanguage string
	row := conn.DB().QueryRow(
		"SELECT source_checksum, original_language FROM strings " +
			"WHERE language_id='es' AND string_id='greeting' AND context=''")
	if err := row.Scan(&checksum, &originalLanguage); err != nil {
		t.Fatal(err)
	}
	if checksum == "" {
		t.Error("expected non-empty source_checksum")
	}
	if originalLanguage != "en" {
		t.Errorf("got original_language=%q, want \"en\"", originalLanguage)
	}
}

func TestSourceRowHasNoChecksum(t *testing.T) {
	conn := freshConn(t, "t9")
	_ = InsertData(conn, "greeting", "en", "Hello world", InsertOptions{})

	var checksum *string
	row := conn.DB().QueryRow(
		"SELECT source_checksum FROM strings WHERE language_id='en' AND string_id='greeting' AND context=''")
	if err := row.Scan(&checksum); err != nil {
		t.Fatal(err)
	}
	if checksum != nil {
		t.Errorf("expected NULL source_checksum, got %q", *checksum)
	}
}

func TestRetrieveRejectsInvalidLanguageID(t *testing.T) {
	conn := freshConn(t, "t10")
	_, _, err := RetrieveData(conn, "greeting", "not-a-real-lang-tag-!!", "")
	if _, ok := err.(*ValidationError); !ok {
		t.Errorf("got err=%v, want *ValidationError", err)
	}
}

func TestInsertRejectsInvalidStatus(t *testing.T) {
	conn := freshConn(t, "t11")
	err := InsertData(conn, "greeting", "en", "Hello", InsertOptions{Status: "live"})
	if _, ok := err.(*ValidationError); !ok {
		t.Errorf("got err=%v, want *ValidationError", err)
	}
}

func TestInsertRejectsEmptyContent(t *testing.T) {
	conn := freshConn(t, "t12")
	err := InsertData(conn, "greeting", "en", "", InsertOptions{})
	if _, ok := err.(*ValidationError); !ok {
		t.Errorf("got err=%v, want *ValidationError", err)
	}
}
