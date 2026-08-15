<?php

declare(strict_types=1);

namespace Multilang;

/**
 * Input validation — every value that reaches SQL goes through here first.
 *
 * All checks are allow-list based (reject anything that doesn't match a
 * known-good shape) rather than deny-list based (reject known-bad patterns).
 *
 * Every id-shaped column (languageId, originalLanguage, stringId, context)
 * is normalized to lowercase by default, always — they're all part of the
 * exact-match composite primary key, so casing differences would otherwise
 * split what should be one row into duplicates.
 *
 * These rules must stay identical to every other language port's
 * validation module — see ../../conformance/README.md.
 */
final class Validation
{
    public const MAX_STRING_ID_LEN = 200;
    public const MAX_CONTEXT_LEN = 200;
    public const MAX_CONTENT_LEN = 65535;
    public const MAX_UPDATED_BY_LEN = 200;
    // The BCP47 pattern below has no upper bound on repeated variant
    // subtags, so without an explicit cap it would accept arbitrarily
    // long tags. This must match every other port's limit exactly (see
    // ../../c/include/multilang.h ML_MAX_LANGUAGE_ID_LEN) or a tag valid
    // in one language could be silently rejected in another.
    public const MAX_LANGUAGE_ID_LEN = 35;

    private const VALID_STATUSES = ['draft', 'reviewed', 'published'];

    // searchData limits — see docs/search.md for the rationale behind
    // these specific numbers and the exact/natural/regex semantics.
    public const MAX_SEARCH_QUERY_LEN = 500;
    public const MIN_SEARCH_LIMIT = 1;
    public const MAX_SEARCH_LIMIT = 500;
    public const DEFAULT_SEARCH_LIMIT = 50;
    private const VALID_SEARCH_MODES = ['exact', 'natural', 'regex'];

    // Simplified BCP 47: primary language (2-3 letters) + optional script
    // (4 letters) + optional region (2 letters or 3 digits) + optional
    // variants. Covers the vast majority of real-world tags: en, es,
    // pt-BR, zh-Hans, zh-Hans-CN, en-US, sr-Latn-RS ...
    //
    // The 'D' modifier (PCRE_DOLLAR_ENDONLY) makes '$' match only the
    // true end of the string — without it, PCRE's default lets '$' match
    // just before a trailing "\n" (Perl-inherited behavior), which would
    // silently accept a tag like "en-US\n" that JavaScript/Go/C's regex
    // engines all reject.
    private const BCP47_RE = '/^[a-zA-Z]{2,3}(-[A-Za-z]{4})?(-([A-Za-z]{2}|[0-9]{3}))?(-[A-Za-z0-9]{5,8})*$/D';

    // stringId / context: namespaced identifiers like "button.publish" or
    // "menu:item-42". Letters, digits, dot, underscore, hyphen, colon.
    // 'D' modifier: see BCP47_RE above.
    private const IDENTIFIER_RE = '/^[A-Za-z0-9._:-]+$/D';

    // "." and ".." both match IDENTIFIER_RE (it allows repeated dots) but
    // are reserved path components on every filesystem the filesystem
    // backend runs on -- stringId/context become directory names there,
    // and either value silently collapses the path back up a level
    // instead of naming a new one. This is the strictest of the three
    // backend families (SQL columns don't care), so it's the shared rule
    // everywhere, not just under the filesystem backend.
    private const RESERVED_IDENTIFIERS = ['.', '..'];

    private function __construct()
    {
    }

    /**
     * ASCII-only lowercasing, deliberately not strtolower(). strtolower()
     * is locale-sensitive (depends on the process's setlocale(LC_CTYPE,
     * ...) state): under a Turkish locale, strtolower('I') returns 'I'
     * unchanged instead of 'i' (Turkish distinguishes dotted/dotless i,
     * and strtolower() can't represent the non-ASCII result in a
     * single-byte string, so it declines to convert at all). Every
     * id-shaped value here is guaranteed ASCII-only by the regex checks
     * that already ran, so this must be a fixed, locale-independent
     * mapping — strtr() with an explicit translation table always is —
     * otherwise this process's normalization could silently disagree
     * with every other language port's (locale-independent) lowercasing
     * for the same input, splitting what should be one row into
     * duplicates.
     */
    private static function asciiLower(string $value): string
    {
        return strtr($value, 'ABCDEFGHIJKLMNOPQRSTUVWXYZ', 'abcdefghijklmnopqrstuvwxyz');
    }

    /**
     * Normalize a BCP 47 tag to lowercase.
     *
     * BCP 47 comparison is defined as case-insensitive, and every
     * id-shaped column in this schema is stored lowercase by default so
     * that casing variation can never split what should be one row into
     * two.
     */
    public static function normalizeLanguageTag(string $value): string
    {
        return self::asciiLower($value);
    }

    /**
     * Validate that $value is a well-formed BCP 47 language tag and
     * return it lowercased.
     *
     * @throws ValidationException If empty or not a valid BCP 47 shape.
     */
    public static function validateLanguageId($value): string
    {
        if (!is_string($value) || $value === '') {
            throw new ValidationException('language_id must be a non-empty string');
        }
        if (strlen($value) > self::MAX_LANGUAGE_ID_LEN) {
            throw new ValidationException("language_id '$value' exceeds " . self::MAX_LANGUAGE_ID_LEN . ' characters');
        }
        if (preg_match(self::BCP47_RE, $value) !== 1) {
            throw new ValidationException("language_id '$value' is not a valid BCP 47 tag");
        }
        return self::normalizeLanguageTag($value);
    }

    /**
     * Like validateLanguageId, but treats null/"" as "not provided" and
     * returns null instead of throwing. Used for originalLanguage, which
     * is empty exactly when a row is the source rather than a translation.
     *
     * @throws ValidationException If provided but not a valid tag.
     */
    public static function validateOptionalLanguageId($value): ?string
    {
        if ($value === null || $value === '') {
            return null;
        }
        return self::validateLanguageId($value);
    }

    /**
     * Validate a stringId: non-empty string, within length limit, and
     * built only from the identifier charset. Returned lowercased since
     * it's part of the exact-match primary key.
     *
     * @throws ValidationException If empty, too long, or has stray chars.
     */
    public static function validateStringId($value): string
    {
        if (!is_string($value) || $value === '') {
            throw new ValidationException('string_id must be a non-empty string');
        }
        if (strlen($value) > self::MAX_STRING_ID_LEN) {
            throw new ValidationException('string_id exceeds ' . self::MAX_STRING_ID_LEN . ' characters');
        }
        if (preg_match(self::IDENTIFIER_RE, $value) !== 1) {
            throw new ValidationException("string_id '$value' contains invalid characters");
        }
        if (in_array($value, self::RESERVED_IDENTIFIERS, true)) {
            throw new ValidationException("string_id '$value' is a reserved path component");
        }
        return self::asciiLower($value);
    }

    /**
     * Validate a context value. null is treated as "no context" and
     * normalized to "" (the default row, matching the composite key's
     * DEFAULT ''). Returned lowercased, same reasoning as stringId.
     *
     * @throws ValidationException If too long or has stray chars.
     */
    public static function validateContext($value): string
    {
        if ($value === null) {
            return '';
        }
        if (!is_string($value)) {
            throw new ValidationException('context must be a string');
        }
        if (strlen($value) > self::MAX_CONTEXT_LEN) {
            throw new ValidationException('context exceeds ' . self::MAX_CONTEXT_LEN . ' characters');
        }
        if ($value !== '' && preg_match(self::IDENTIFIER_RE, $value) !== 1) {
            throw new ValidationException("context '$value' contains invalid characters");
        }
        if (in_array($value, self::RESERVED_IDENTIFIERS, true)) {
            throw new ValidationException("context '$value' is a reserved path component");
        }
        return self::asciiLower($value);
    }

    /**
     * Validate the text to be stored. Rejects NUL bytes since some
     * backends (and C callers) treat them as string terminators.
     *
     * MAX_CONTENT_LEN is measured in bytes — PHP strings are byte arrays,
     * so strlen() already does the right thing here without any
     * encoding-aware counting. This must stay bytes, not characters, to
     * match every other language port (see ../../conformance/README.md):
     * Python/JavaScript measure content length in codepoints/UTF-16 units
     * by default and have to explicitly convert to UTF-8 byte length to
     * agree with PHP/Go/C, which measure bytes natively.
     *
     * @throws ValidationException If empty, too long, or contains NUL.
     */
    public static function validateContent($value): string
    {
        if (!is_string($value) || $value === '') {
            throw new ValidationException('content must be a non-empty string');
        }
        if (strlen($value) > self::MAX_CONTENT_LEN) {
            throw new ValidationException('content exceeds ' . self::MAX_CONTENT_LEN . ' bytes');
        }
        if (strpos($value, "\0") !== false) {
            throw new ValidationException('content must not contain NUL bytes');
        }
        return $value;
    }

    /**
     * Validate that $value is one of the allowed workflow states.
     *
     * @throws ValidationException If not one of draft/reviewed/published.
     */
    public static function validateStatus($value): string
    {
        if (!in_array($value, self::VALID_STATUSES, true)) {
            $allowed = implode(', ', self::VALID_STATUSES);
            throw new ValidationException("status must be one of [$allowed] — got " . var_export($value, true));
        }
        return $value;
    }

    /**
     * Validate the optional audit-trail field identifying who/what wrote
     * a row.
     *
     * Like content, updated_by has no charset restriction, so
     * MAX_UPDATED_BY_LEN is measured in bytes — PHP strings are byte
     * arrays, so strlen() already does the right thing here, matching
     * every other port (see ../../conformance/README.md).
     *
     * @throws ValidationException If not a string or exceeds the length limit.
     */
    public static function validateUpdatedBy($value): ?string
    {
        if ($value === null) {
            return null;
        }
        if (!is_string($value)) {
            throw new ValidationException('updated_by must be a string');
        }
        if (strlen($value) > self::MAX_UPDATED_BY_LEN) {
            throw new ValidationException('updated_by exceeds ' . self::MAX_UPDATED_BY_LEN . ' bytes');
        }
        return $value;
    }

    /**
     * Validate that $value is one of searchData's three modes.
     *
     * @throws ValidationException If not one of exact/natural/regex.
     */
    public static function validateSearchMode($value): string
    {
        if (!is_string($value) || !in_array($value, self::VALID_SEARCH_MODES, true)) {
            $allowed = implode(', ', self::VALID_SEARCH_MODES);
            throw new ValidationException("mode must be one of [$allowed] — got " . var_export($value, true));
        }
        return $value;
    }

    /**
     * Wrap a caller's raw pattern (no delimiters, same shape Python's
     * re.compile/Go's regexp.Compile/JS's `new RegExp` take) into a
     * delimited PCRE pattern ready for preg_match, and confirm it
     * compiles.
     *
     * The 'u' (UTF-8) modifier is not optional here: without it PCRE
     * treats the subject as raw bytes, so a metacharacter like '.' would
     * match half of a multi-byte UTF-8 character instead of one whole
     * character — silently wrong for any non-ASCII content, not just a
     * documented cross-language nuance like the regex-flavor differences
     * elsewhere in this file.
     *
     * @throws ValidationException If no unused delimiter can be found, or
     *   the pattern fails to compile.
     */
    private static function compileRegexPattern(string $value, bool $caseSensitive): string
    {
        $delimiter = null;
        foreach (['~', '#', '%', '!', "\x01"] as $candidate) {
            if (strpos($value, $candidate) === false) {
                $delimiter = $candidate;
                break;
            }
        }
        if ($delimiter === null) {
            throw new ValidationException('query is not a valid regex: no available pattern delimiter');
        }

        $pattern = $delimiter . $value . $delimiter . 'u' . ($caseSensitive ? '' : 'i');
        if (@preg_match($pattern, '') === false) {
            throw new ValidationException('query is not a valid regex: ' . preg_last_error_msg());
        }
        return $pattern;
    }

    /**
     * Validate a searchData query and pre-process it into the form the
     * matcher for $mode actually needs, so searchData doesn't re-derive
     * it per row.
     *
     * @return array{query: string, terms: ?array, pattern: ?string} terms
     *   is a non-empty whitespace-split array for "natural", pattern is a
     *   delimited PCRE pattern (ready for preg_match) for "regex"; both
     *   null otherwise.
     * @throws ValidationException If $value is empty/too long/contains
     *   NUL, if $mode="natural" and $value has no non-whitespace terms,
     *   or if $mode="regex" and $value fails to compile.
     */
    public static function validateSearchQuery($value, string $mode, bool $caseSensitive): array
    {
        if (!is_string($value) || $value === '') {
            throw new ValidationException('query must be a non-empty string');
        }
        if (strlen($value) > self::MAX_SEARCH_QUERY_LEN) {
            throw new ValidationException('query exceeds ' . self::MAX_SEARCH_QUERY_LEN . ' bytes');
        }
        if (strpos($value, "\0") !== false) {
            throw new ValidationException('query must not contain NUL bytes');
        }

        if ($mode === 'regex') {
            return ['query' => $value, 'terms' => null, 'pattern' => self::compileRegexPattern($value, $caseSensitive)];
        }

        if ($mode === 'natural') {
            $terms = preg_split('/\s+/', trim($value), -1, PREG_SPLIT_NO_EMPTY);
            if (empty($terms)) {
                throw new ValidationException('query must contain at least one term in natural mode');
            }
            return ['query' => $value, 'terms' => $terms, 'pattern' => null];
        }

        return ['query' => $value, 'terms' => null, 'pattern' => null];
    }

    /**
     * Validate searchData's limit/offset.
     *
     * @return array{limit: int, offset: int}
     * @throws ValidationException If $limit isn't an integer in
     *   [MIN_SEARCH_LIMIT, MAX_SEARCH_LIMIT], or $offset isn't a
     *   non-negative integer.
     */
    public static function validateSearchPagination($limit, $offset): array
    {
        if (!is_int($limit) || $limit < self::MIN_SEARCH_LIMIT || $limit > self::MAX_SEARCH_LIMIT) {
            throw new ValidationException(
                'limit must be an integer between ' . self::MIN_SEARCH_LIMIT . ' and ' . self::MAX_SEARCH_LIMIT .
                ' — got ' . var_export($limit, true)
            );
        }
        if (!is_int($offset) || $offset < 0) {
            throw new ValidationException('offset must be a non-negative integer — got ' . var_export($offset, true));
        }
        return ['limit' => $limit, 'offset' => $offset];
    }
}
