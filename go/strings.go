package multilang

// The two public data functions: RetrieveData and InsertData.
//
// Both take an already-open Backend (from Connect) so the caller controls
// connection lifetime; neither function opens or closes a connection
// itself.

import (
	"crypto/sha256"
	"encoding/hex"
	"time"
)

// RetrieveData looks up one piece of text by its identity.
//
// Every value is validated before it reaches SQL and every query is
// parameterized — no value here is ever concatenated into a query string.
//
// context defaults to "" (pass "" for the un-contextualized row).
//
// Returns the stored content and whether a row was found — the data only,
// no metadata.
func RetrieveData(conn Backend, stringID, languageID, context string) (string, bool, error) {
	stringID, err := ValidateStringID(stringID)
	if err != nil {
		return "", false, err
	}
	languageID, err = ValidateLanguageID(languageID)
	if err != nil {
		return "", false, err
	}
	context, err = ValidateContext(context)
	if err != nil {
		return "", false, err
	}

	return conn.SelectContent(stringID, languageID, context)
}

// InsertOptions holds InsertData's optional parameters. Zero values mean
// "not set": Context defaults to "", OriginalLanguage/UpdatedBy default to
// unset (NULL), Status defaults to "draft" if left "".
type InsertOptions struct {
	Context          string
	OriginalLanguage string
	Status           string
	UpdatedBy        string
}

// InsertData inserts a new row, or updates it in place if (stringID,
// languageID, context) already exists (upsert on the composite primary
// key).
//
// When opts.OriginalLanguage is given, the current content of the source
// row (languageID=opts.OriginalLanguage, same stringID/context) is hashed
// with SHA-256 and stored as source_checksum, so staleness can be
// detected later by re-hashing the source and comparing. If the source
// row doesn't exist yet, source_checksum is left unset.
func InsertData(conn Backend, stringID, languageID, content string, opts InsertOptions) error {
	status := opts.Status
	if status == "" {
		status = "draft"
	}

	stringID, err := ValidateStringID(stringID)
	if err != nil {
		return err
	}
	languageID, err = ValidateLanguageID(languageID)
	if err != nil {
		return err
	}
	context, err := ValidateContext(opts.Context)
	if err != nil {
		return err
	}
	content, err = ValidateContent(content)
	if err != nil {
		return err
	}
	originalLanguage, err := ValidateOptionalLanguageID(opts.OriginalLanguage)
	if err != nil {
		return err
	}
	status, err = ValidateStatus(status)
	if err != nil {
		return err
	}
	updatedBy, err := ValidateUpdatedBy(opts.UpdatedBy)
	if err != nil {
		return err
	}

	var sourceChecksum string
	if originalLanguage != "" {
		sourceContent, found, err := conn.SelectContent(stringID, originalLanguage, context)
		if err != nil {
			return err
		}
		if found {
			sourceChecksum = checksum(sourceContent)
		}
	}

	return conn.Upsert(Row{
		StringID:         stringID,
		LanguageID:       languageID,
		Context:          context,
		Content:          content,
		OriginalLanguage: originalLanguage,
		Status:           status,
		SourceChecksum:   sourceChecksum,
		UpdatedBy:        updatedBy,
		// A real time.Time, not a pre-formatted string: each backend
		// adapts/formats it to that database's native timestamp
		// expectations itself (see the comments in each backend file).
		DateUpdated: time.Now().UTC(),
	})
}

// checksum returns the hex SHA-256 digest of text, used for source_checksum.
func checksum(text string) string {
	sum := sha256.Sum256([]byte(text))
	return hex.EncodeToString(sum[:])
}
