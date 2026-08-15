package multilang

// The two public data functions: RetrieveData and InsertData.
//
// Both take an already-open Backend (from Connect) so the caller controls
// connection lifetime; neither function opens or closes a connection
// itself.

import (
	"crypto/sha256"
	"encoding/hex"
	"regexp"
	"sort"
	"strings"
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

// SearchMode identifies which of SearchData's three matching algorithms
// to use — see docs/search.md.
type SearchMode string

const (
	SearchModeExact   SearchMode = "exact"
	SearchModeNatural SearchMode = "natural"
	SearchModeRegex   SearchMode = "regex"
)

// SearchOptions holds SearchData's optional parameters. Zero values mean
// "not set": LanguageID/Status "" mean no filter, Context nil means no
// filter (a non-nil pointer to "" filters for only the
// default/un-contextualized row — "" can't double as both "no filter"
// and "a real filter value" the way it can for LanguageID/Status, which
// are never valid as ""), CaseSensitive false, Limit 0 meaning the
// default (DefaultSearchLimit), Offset 0.
type SearchOptions struct {
	LanguageID    string
	Context       *string
	Status        string
	CaseSensitive bool
	Limit         int
	Offset        int
}

// SearchData searches Content across every row matching opts' optional
// filters.
//
// Matching runs entirely in-process, after fetching candidate rows from
// the backend filtered only by the cheap exact-match columns
// (LanguageID/Context/Status) — this is what guarantees identical search
// results across SQLite/Postgres/MySQL/filesystem: the matching logic
// below never touches backend-specific SQL/FTS engines. See
// docs/search.md for the full rationale and the documented
// cross-language regex-flavor/case-folding limitations.
//
//   - mode="exact": query is a literal substring of Content.
//   - mode="natural" (typical default): query is split on whitespace
//     into terms, every one of which must appear as a substring of
//     Content — AND, not OR.
//   - mode="regex": query is a Go RE2 pattern (regexp package syntax)
//     searched against Content.
//
// Results are ordered by match score descending, then
// (LanguageID, StringID, Context) ascending as a deterministic tiebreak.
func SearchData(conn Backend, query string, mode SearchMode, opts SearchOptions) ([]Row, error) {
	mode, err := ValidateSearchMode(mode)
	if err != nil {
		return nil, err
	}
	query, terms, pattern, err := ValidateSearchQuery(query, mode, opts.CaseSensitive)
	if err != nil {
		return nil, err
	}

	languageID := opts.LanguageID
	if languageID != "" {
		languageID, err = ValidateOptionalLanguageID(languageID)
		if err != nil {
			return nil, err
		}
	}
	var context *string
	if opts.Context != nil {
		validated, cErr := ValidateContext(*opts.Context)
		if cErr != nil {
			return nil, cErr
		}
		context = &validated
	}
	status := opts.Status
	if status != "" {
		status, err = ValidateStatus(status)
		if err != nil {
			return nil, err
		}
	}

	limit := opts.Limit
	if limit == 0 {
		limit = DefaultSearchLimit
	}
	limit, offset, err := ValidateSearchPagination(limit, opts.Offset)
	if err != nil {
		return nil, err
	}

	rows, err := conn.SelectRows(languageID, status, context)
	if err != nil {
		return nil, err
	}

	type scoredRow struct {
		score int
		row   Row
	}
	scored := make([]scoredRow, 0, len(rows))
	for _, row := range rows {
		score := scoreRow(row.Content, mode, query, terms, pattern, opts.CaseSensitive)
		if score > 0 {
			scored = append(scored, scoredRow{score, row})
		}
	}

	sort.Slice(scored, func(i, j int) bool {
		if scored[i].score != scored[j].score {
			return scored[i].score > scored[j].score
		}
		if scored[i].row.LanguageID != scored[j].row.LanguageID {
			return scored[i].row.LanguageID < scored[j].row.LanguageID
		}
		if scored[i].row.StringID != scored[j].row.StringID {
			return scored[i].row.StringID < scored[j].row.StringID
		}
		return scored[i].row.Context < scored[j].row.Context
	})

	if offset >= len(scored) {
		return []Row{}, nil
	}
	end := offset + limit
	if end > len(scored) {
		end = len(scored)
	}
	result := make([]Row, 0, end-offset)
	for _, sr := range scored[offset:end] {
		result = append(result, sr.row)
	}
	return result, nil
}

// scoreRow returns how many times query (or, for natural/regex, its
// pre-processed form terms/pattern) matches content under mode — 0 means
// no match. See docs/search.md for the exact/natural/regex semantics.
func scoreRow(content string, mode SearchMode, query string, terms []string, pattern *regexp.Regexp, caseSensitive bool) int {
	if mode == SearchModeRegex {
		return len(pattern.FindAllStringIndex(content, -1))
	}

	haystack := content
	if !caseSensitive {
		haystack = asciiFold(content)
	}

	if mode == SearchModeExact {
		needle := query
		if !caseSensitive {
			needle = asciiFold(query)
		}
		return countOccurrences(haystack, needle)
	}

	// natural: every term must appear at least once (AND); score is the
	// sum of each term's occurrence count.
	total := 0
	for _, term := range terms {
		needle := term
		if !caseSensitive {
			needle = asciiFold(term)
		}
		occurrences := countOccurrences(haystack, needle)
		if occurrences == 0 {
			return 0
		}
		total += occurrences
	}
	return total
}

// asciiFold lowercases only the ASCII A-Z range, leaving every other
// byte untouched. Deliberately not strings.ToLower (Unicode-aware) — see
// the Python port's _ascii_fold for why search's case-insensitive
// matching must stay ASCII-only across every language, C included.
func asciiFold(text string) string {
	b := []byte(text)
	for i, c := range b {
		if c >= 'A' && c <= 'Z' {
			b[i] = c + 32
		}
	}
	return string(b)
}

// countOccurrences counts non-overlapping occurrences of needle in haystack.
func countOccurrences(haystack, needle string) int {
	if needle == "" {
		return 0
	}
	count := 0
	start := 0
	for {
		idx := strings.Index(haystack[start:], needle)
		if idx == -1 {
			return count
		}
		count++
		start += idx + len(needle)
	}
}
