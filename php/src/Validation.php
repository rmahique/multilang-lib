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
}
