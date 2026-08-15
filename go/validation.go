package multilang

// Input validation — every value that reaches SQL goes through here first.
//
// All checks are allow-list based (reject anything that doesn't match a
// known-good shape) rather than deny-list based (reject known-bad
// patterns).
//
// Every id-shaped column (LanguageID, OriginalLanguage, StringID, Context)
// is normalized to lowercase by default, always — they're all part of the
// exact-match composite primary key, so casing differences would
// otherwise split what should be one row into duplicates.
//
// These rules must stay identical to every other language port's
// validation module — see ../conformance/README.md.

import (
	"fmt"
	"regexp"
	"strings"
)

const (
	MaxStringIDLen  = 200
	MaxContextLen   = 200
	MaxContentLen   = 65535
	MaxUpdatedByLen = 200
	// The BCP47 pattern below has no upper bound on repeated variant
	// subtags, so without an explicit cap it would accept arbitrarily
	// long tags. This must match every other port's limit exactly (see
	// ../c/include/multilang.h ML_MAX_LANGUAGE_ID_LEN) or a tag valid in
	// one language could be silently rejected in another.
	MaxLanguageIDLen = 35
)

var validStatuses = map[string]bool{"draft": true, "reviewed": true, "published": true}

// search_data limits — see docs/search.md for the rationale behind these
// specific numbers and the exact/natural/regex semantics.
const (
	MaxSearchQueryLen  = 500
	MinSearchLimit     = 1
	MaxSearchLimit     = 500
	DefaultSearchLimit = 50
)

// Simplified BCP 47: primary language (2-3 letters) + optional script
// (4 letters) + optional region (2 letters or 3 digits) + optional
// variants. Covers the vast majority of real-world tags: en, es, pt-BR,
// zh-Hans, zh-Hans-CN, en-US, sr-Latn-RS ...
var bcp47RE = regexp.MustCompile(`^[a-zA-Z]{2,3}(-[A-Za-z]{4})?(-([A-Za-z]{2}|[0-9]{3}))?(-[A-Za-z0-9]{5,8})*$`)

// StringID / context: namespaced identifiers like "button.publish" or
// "menu:item-42". Letters, digits, dot, underscore, hyphen, colon.
var identifierRE = regexp.MustCompile(`^[A-Za-z0-9._:-]+$`)

// "." and ".." both match identifierRE (it allows repeated dots) but are
// reserved path components on every filesystem the filesystem backend
// runs on -- string_id/context become directory names there, and either
// value silently collapses the path back up a level instead of naming a
// new one. This is the strictest of the three backend families (SQL
// columns don't care), so it's the shared rule everywhere, not just
// under the filesystem backend.
var reservedIdentifiers = map[string]bool{".": true, "..": true}

// ValidationError is returned when caller-supplied data fails validation.
type ValidationError struct {
	msg string
}

func (e *ValidationError) Error() string { return e.msg }

func newValidationError(format string, args ...interface{}) *ValidationError {
	return &ValidationError{msg: fmt.Sprintf(format, args...)}
}

// NormalizeLanguageTag lowercases a BCP 47 tag.
//
// BCP 47 comparison is defined as case-insensitive, and every id-shaped
// column in this schema is stored lowercase by default so that casing
// variation can never split what should be one row into two.
func NormalizeLanguageTag(value string) string {
	return strings.ToLower(value)
}

// ValidateLanguageID validates that value is a well-formed BCP 47 language
// tag and returns it lowercased.
func ValidateLanguageID(value string) (string, error) {
	if value == "" {
		return "", newValidationError("language_id must be a non-empty string")
	}
	if len(value) > MaxLanguageIDLen {
		return "", newValidationError("language_id %q exceeds %d characters", value, MaxLanguageIDLen)
	}
	if !bcp47RE.MatchString(value) {
		return "", newValidationError("language_id %q is not a valid BCP 47 tag", value)
	}
	return NormalizeLanguageTag(value), nil
}

// ValidateOptionalLanguageID is like ValidateLanguageID, but treats "" as
// "not provided" and returns ("", nil) instead of erroring. Used for
// OriginalLanguage, which is empty exactly when a row is the source
// rather than a translation.
func ValidateOptionalLanguageID(value string) (string, error) {
	if value == "" {
		return "", nil
	}
	return ValidateLanguageID(value)
}

// ValidateStringID validates a string_id: non-empty, within length limit,
// and built only from the identifier charset. Returned lowercased since
// it's part of the exact-match primary key.
func ValidateStringID(value string) (string, error) {
	if value == "" {
		return "", newValidationError("string_id must be a non-empty string")
	}
	if len(value) > MaxStringIDLen {
		return "", newValidationError("string_id exceeds %d characters", MaxStringIDLen)
	}
	if !identifierRE.MatchString(value) {
		return "", newValidationError("string_id %q contains invalid characters", value)
	}
	if reservedIdentifiers[value] {
		return "", newValidationError("string_id %q is a reserved path component", value)
	}
	return strings.ToLower(value), nil
}

// ValidateContext validates a context value. "" means "no context" and
// stays "" (the default row, matching the composite key's DEFAULT ”).
// Returned lowercased, same reasoning as StringID.
func ValidateContext(value string) (string, error) {
	if value == "" {
		return "", nil
	}
	if len(value) > MaxContextLen {
		return "", newValidationError("context exceeds %d characters", MaxContextLen)
	}
	if !identifierRE.MatchString(value) {
		return "", newValidationError("context %q contains invalid characters", value)
	}
	if reservedIdentifiers[value] {
		return "", newValidationError("context %q is a reserved path component", value)
	}
	return strings.ToLower(value), nil
}

// ValidateContent validates the text to be stored. Rejects NUL bytes
// since some backends (and C callers) treat them as string terminators.
//
// MaxContentLen is measured in bytes — Go strings are byte slices, so
// len(value) already does the right thing here without any
// encoding-aware counting. This must stay bytes, not characters, to
// match every other language port (see ../conformance/README.md):
// Python/JavaScript measure content length in codepoints/UTF-16 units by
// default and have to explicitly convert to UTF-8 byte length to agree
// with Go/PHP/C, which measure bytes natively.
func ValidateContent(value string) (string, error) {
	if value == "" {
		return "", newValidationError("content must be a non-empty string")
	}
	if len(value) > MaxContentLen {
		return "", newValidationError("content exceeds %d bytes", MaxContentLen)
	}
	if strings.ContainsRune(value, '\x00') {
		return "", newValidationError("content must not contain NUL bytes")
	}
	return value, nil
}

// ValidateStatus validates that value is one of the allowed workflow
// states.
func ValidateStatus(value string) (string, error) {
	if !validStatuses[value] {
		return "", newValidationError("status must be one of [draft, reviewed, published] — got %q", value)
	}
	return value, nil
}

// ValidateUpdatedBy validates the optional audit-trail field identifying
// who/what wrote a row. "" is treated as "not provided".
func ValidateUpdatedBy(value string) (string, error) {
	if value == "" {
		return "", nil
	}
	if len(value) > MaxUpdatedByLen {
		return "", newValidationError("updated_by exceeds %d characters", MaxUpdatedByLen)
	}
	return value, nil
}

var validSearchModes = map[SearchMode]bool{
	SearchModeExact: true, SearchModeNatural: true, SearchModeRegex: true,
}

// ValidateSearchMode validates that mode is one of SearchData's three
// modes.
func ValidateSearchMode(mode SearchMode) (SearchMode, error) {
	if !validSearchModes[mode] {
		return "", newValidationError("mode must be one of [exact, natural, regex] — got %q", mode)
	}
	return mode, nil
}

// ValidateSearchQuery validates a SearchData query and pre-processes it
// into the form the matcher for mode actually needs, so SearchData
// doesn't re-derive it per row.
//
// Exactly one of the two extra return values is populated: terms for
// "natural" (non-empty, whitespace-split), pattern for "regex" (compiled
// with case-insensitivity baked in via caseSensitive); both are nil for
// "exact".
func ValidateSearchQuery(value string, mode SearchMode, caseSensitive bool) (query string, terms []string, pattern *regexp.Regexp, err error) {
	if value == "" {
		return "", nil, nil, newValidationError("query must be a non-empty string")
	}
	if len(value) > MaxSearchQueryLen {
		return "", nil, nil, newValidationError("query exceeds %d bytes", MaxSearchQueryLen)
	}
	if strings.ContainsRune(value, '\x00') {
		return "", nil, nil, newValidationError("query must not contain NUL bytes")
	}

	switch mode {
	case SearchModeRegex:
		pat := value
		if !caseSensitive {
			pat = "(?i)" + pat
		}
		re, compileErr := regexp.Compile(pat)
		if compileErr != nil {
			return "", nil, nil, newValidationError("query is not a valid regex: %s", compileErr)
		}
		return value, nil, re, nil
	case SearchModeNatural:
		fields := strings.Fields(value)
		if len(fields) == 0 {
			return "", nil, nil, newValidationError("query must contain at least one term in natural mode")
		}
		return value, fields, nil, nil
	default:
		return value, nil, nil, nil
	}
}

// ValidateSearchPagination validates SearchData's limit/offset.
func ValidateSearchPagination(limit, offset int) (int, int, error) {
	if limit < MinSearchLimit || limit > MaxSearchLimit {
		return 0, 0, newValidationError("limit must be between %d and %d — got %d", MinSearchLimit, MaxSearchLimit, limit)
	}
	if offset < 0 {
		return 0, 0, newValidationError("offset must be a non-negative integer — got %d", offset)
	}
	return limit, offset, nil
}
