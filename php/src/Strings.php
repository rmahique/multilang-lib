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
}
