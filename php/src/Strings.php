<?php

declare(strict_types=1);

namespace Multilang;

use DateTimeImmutable;
use DateTimeZone;
use Multilang\Backends\BackendInterface;

/**
 * The two public data functions, as static methods: retrieveData and
 * insertData.
 *
 * Both take an already-open backend (from Connector::connect) so the
 * caller controls connection lifetime; neither method opens or closes a
 * connection itself.
 */
final class Strings
{
    private function __construct()
    {
    }

    /**
     * Look up one piece of text by its identity.
     *
     * Every value is validated before it reaches SQL and every query is
     * parameterized — no value here is ever concatenated into a query
     * string.
     *
     * @param BackendInterface $conn An open backend instance from Connector::connect.
     * @param string $stringId Identifier of the string to fetch.
     * @param string $languageId BCP 47 tag of the language to fetch (e.g. "es").
     * @param string $context Optional disambiguator for stringIds reused
     *   with different wording in different places. Defaults to "".
     * @return string|null The stored content, or null if no matching row
     *   exists. Data only — no metadata is returned.
     * @throws ValidationException If any argument fails validation.
     */
    public static function retrieveData(
        BackendInterface $conn,
        string $stringId,
        string $languageId,
        string $context = ''
    ): ?string {
        $stringId = Validation::validateStringId($stringId);
        $languageId = Validation::validateLanguageId($languageId);
        $context = Validation::validateContext($context);

        return $conn->selectContent($stringId, $languageId, $context);
    }

    /**
     * Insert a new row, or update it in place if (stringId, languageId,
     * context) already exists (upsert on the composite primary key).
     *
     * @param BackendInterface $conn An open backend instance from Connector::connect.
     * @param string $stringId Identifier of the string being written.
     * @param string $languageId BCP 47 tag of the language this content is written in.
     * @param string $content The text to store.
     * @param string $context Optional disambiguator; see retrieveData. Defaults to "".
     * @param string|null $originalLanguage BCP 47 tag of the source
     *   language, if this row is a translation. Leave null when this row
     *   IS the source — no source_checksum is then computed.
     * @param string $status Workflow state — one of "draft", "reviewed",
     *   "published". Defaults to "draft".
     * @param string|null $updatedBy Optional identifier of who/what wrote
     *   this row, for an audit trail.
     *
     * When $originalLanguage is given, the current content of the source
     * row (languageId=$originalLanguage, same stringId/context) is hashed
     * with SHA-256 and stored as source_checksum, so staleness can be
     * detected later by re-hashing the source and comparing. If the
     * source row doesn't exist yet, source_checksum is left null.
     *
     * @throws ValidationException If any argument fails validation.
     */
    public static function insertData(
        BackendInterface $conn,
        string $stringId,
        string $languageId,
        string $content,
        string $context = '',
        ?string $originalLanguage = null,
        string $status = 'draft',
        ?string $updatedBy = null
    ): void {
        $stringId = Validation::validateStringId($stringId);
        $languageId = Validation::validateLanguageId($languageId);
        $context = Validation::validateContext($context);
        $content = Validation::validateContent($content);
        $originalLanguage = Validation::validateOptionalLanguageId($originalLanguage);
        $status = Validation::validateStatus($status);
        $updatedBy = Validation::validateUpdatedBy($updatedBy);

        $sourceChecksum = null;
        if ($originalLanguage !== null) {
            $sourceContent = $conn->selectContent($stringId, $originalLanguage, $context);
            if ($sourceContent !== null) {
                $sourceChecksum = hash('sha256', $sourceContent);
            }
        }

        $conn->upsert([
            'string_id' => $stringId,
            'language_id' => $languageId,
            'context' => $context,
            'content' => $content,
            'original_language' => $originalLanguage,
            'status' => $status,
            'source_checksum' => $sourceChecksum,
            'updated_by' => $updatedBy,
            // A real DateTimeImmutable, not a pre-formatted string: each
            // backend formats it to that database's native timestamp
            // expectations itself (see the "date_updated" comments in
            // each Backends/*.php).
            'date_updated' => new DateTimeImmutable('now', new DateTimeZone('UTC')),
        ]);
    }

    /**
     * Search content across every row matching the optional filters.
     *
     * Matching runs entirely in-process, after fetching candidate rows
     * from the backend filtered only by the cheap exact-match columns
     * ($languageId/$context/$status) — this is what guarantees identical
     * search results across SQLite/Postgres/MySQL/filesystem: the actual
     * matching logic below never touches backend-specific SQL/FTS
     * engines. See docs/search.md for the full rationale and the
     * documented cross-language regex-flavor/case-folding limitations.
     *
     * @param BackendInterface $conn An open backend instance from Connector::connect.
     * @param string $query The text/pattern to search for. Non-empty,
     *   at most 500 UTF-8 bytes.
     * @param string $mode "exact" (query is a literal substring of
     *   content), "natural" (default; query is split on whitespace into
     *   terms, every one of which must appear as a substring of content —
     *   AND, not OR), or "regex" (query is a PCRE pattern, given without
     *   delimiters, searched against content in UTF-8 mode).
     * @param string|null $languageId Optional exact-match filter. null
     *   (default) means no filter.
     * @param string|null $context Optional exact-match filter. null
     *   (default) means no filter; '' filters for only the
     *   default/un-contextualized row.
     * @param string|null $status Optional exact-match filter. null
     *   (default) means no filter.
     * @param bool $caseSensitive If false (default), exact/natural
     *   matching folds ASCII letters only (non-ASCII letters always match
     *   by exact case — a documented limitation, see docs/search.md) and
     *   regex matching uses PCRE's 'i' modifier.
     * @param int $limit Maximum rows to return, 1-500. Defaults to 50.
     * @param int $offset Rows to skip before the first returned result,
     *   for pagination. Defaults to 0.
     * @return array[] Rows — string_id, language_id, context, content,
     *   original_language, status, source_checksum, updated_by,
     *   date_updated — matching the filters and query, ordered by match
     *   score descending, then (language_id, string_id, context)
     *   ascending as a deterministic tiebreak.
     * @throws ValidationException If any argument fails validation.
     */
    public static function searchData(
        BackendInterface $conn,
        string $query,
        string $mode = 'natural',
        ?string $languageId = null,
        ?string $context = null,
        ?string $status = null,
        bool $caseSensitive = false,
        int $limit = 50,
        int $offset = 0
    ): array {
        $mode = Validation::validateSearchMode($mode);
        $parsed = Validation::validateSearchQuery($query, $mode, $caseSensitive);
        $query = $parsed['query'];
        $terms = $parsed['terms'];
        $pattern = $parsed['pattern'];

        $validLanguageId = Validation::validateOptionalLanguageId($languageId);
        $validContext = $context === null ? null : Validation::validateContext($context);
        $validStatus = $status === null ? null : Validation::validateStatus($status);
        $pagination = Validation::validateSearchPagination($limit, $offset);
        $limit = $pagination['limit'];
        $offset = $pagination['offset'];

        $rows = $conn->selectRows($validLanguageId, $validContext, $validStatus);

        $scored = [];
        foreach ($rows as $row) {
            $score = self::scoreRow($row['content'], $mode, $query, $terms, $pattern, $caseSensitive);
            if ($score > 0) {
                $scored[] = ['score' => $score, 'row' => $row];
            }
        }

        usort($scored, static function (array $a, array $b): int {
            if ($a['score'] !== $b['score']) {
                return $b['score'] <=> $a['score'];
            }
            $ra = $a['row'];
            $rb = $b['row'];
            return [$ra['language_id'], $ra['string_id'], $ra['context']]
                <=> [$rb['language_id'], $rb['string_id'], $rb['context']];
        });

        $page = array_slice($scored, $offset, $limit);
        return array_map(static fn (array $s) => $s['row'], $page);
    }

    /**
     * Return how many times $query (or, for natural/regex, its
     * pre-processed form $terms/$pattern) matches $content under $mode —
     * 0 means no match. See docs/search.md for the exact/natural/regex
     * semantics.
     */
    private static function scoreRow(
        string $content,
        string $mode,
        string $query,
        ?array $terms,
        ?string $pattern,
        bool $caseSensitive
    ): int {
        if ($mode === 'regex') {
            $count = preg_match_all($pattern, $content);
            return $count === false ? 0 : $count;
        }

        $haystack = $caseSensitive ? $content : self::asciiFold($content);

        if ($mode === 'exact') {
            $needle = $caseSensitive ? $query : self::asciiFold($query);
            return self::countOccurrences($haystack, $needle);
        }

        // natural: every term must appear at least once (AND); score is
        // the sum of each term's occurrence count.
        $total = 0;
        foreach ($terms as $term) {
            $needle = $caseSensitive ? $term : self::asciiFold($term);
            $occurrences = self::countOccurrences($haystack, $needle);
            if ($occurrences === 0) {
                return 0;
            }
            $total += $occurrences;
        }
        return $total;
    }

    /**
     * Lowercase only the ASCII A-Z range, leaving every other byte
     * untouched — deliberately not strtolower() (locale-sensitive) or
     * mb_strtolower() (full Unicode fold). See Validation::asciiLower's
     * docblock for why this project never trusts a language's own
     * "smart" lowercasing for cross-language guarantees; docs/search.md
     * documents the resulting limitation that non-ASCII letters only
     * match by exact case.
     */
    private static function asciiFold(string $text): string
    {
        return strtr($text, 'ABCDEFGHIJKLMNOPQRSTUVWXYZ', 'abcdefghijklmnopqrstuvwxyz');
    }

    /** Count non-overlapping occurrences of $needle in $haystack. */
    private static function countOccurrences(string $haystack, string $needle): int
    {
        if ($needle === '') {
            return 0;
        }
        $count = 0;
        $start = 0;
        while (($pos = strpos($haystack, $needle, $start)) !== false) {
            $count++;
            $start = $pos + strlen($needle);
        }
        return $count;
    }
}
