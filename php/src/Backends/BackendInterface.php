<?php

declare(strict_types=1);

namespace Multilang\Backends;

/**
 * Common interface every backend (SQLite, PostgreSQL, MySQL, filesystem)
 * must implement. All SQL lives here — callers never see raw SQL or the
 * raw PDO connection. Every method must use parameterized queries; no
 * value is ever interpolated into a query string. (The filesystem
 * backend has no SQL/PDO connection, but implements the same interface
 * shape for the same reason: callers never see its storage details
 * either.)
 */
interface BackendInterface
{
    /** Create the `strings` table if it doesn't already exist. */
    public function ensureSchema(): void;

    /** Return content for the given key, or null if no row matches. */
    public function selectContent(string $stringId, string $languageId, string $context): ?string;

    /**
     * Insert `row`, or update it in place on primary-key conflict.
     * $row keys: string_id, language_id, context, content,
     * original_language, status, source_checksum, updated_by, date_updated.
     */
    public function upsert(array $row): void;

    /** Close the underlying connection. */
    public function close(): void;
}
