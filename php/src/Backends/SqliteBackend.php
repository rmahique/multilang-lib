<?php

declare(strict_types=1);

namespace Multilang\Backends;

use DateTimeImmutable;
use PDO;

/** SQLite backend, via PDO — the zero-setup default, and what the test suite uses. */
final class SqliteBackend implements BackendInterface
{
    private const SCHEMA = <<<SQL
CREATE TABLE IF NOT EXISTS strings (
    language_id       TEXT NOT NULL,
    string_id         TEXT NOT NULL,
    context           TEXT NOT NULL DEFAULT '',
    content           TEXT NOT NULL,
    original_language TEXT,
    status            TEXT NOT NULL DEFAULT 'draft',
    source_checksum   TEXT,
    updated_by        TEXT,
    date_updated      TEXT NOT NULL,
    PRIMARY KEY (language_id, string_id, context)
)
SQL;

    private const UPSERT = <<<SQL
INSERT INTO strings
    (language_id, string_id, context, content, original_language,
     status, source_checksum, updated_by, date_updated)
VALUES (:language_id, :string_id, :context, :content, :original_language,
        :status, :source_checksum, :updated_by, :date_updated)
ON CONFLICT(language_id, string_id, context) DO UPDATE SET
    content=excluded.content,
    original_language=excluded.original_language,
    status=excluded.status,
    source_checksum=excluded.source_checksum,
    updated_by=excluded.updated_by,
    date_updated=excluded.date_updated
SQL;

    private const SELECT = <<<SQL
SELECT content FROM strings
WHERE language_id = :language_id AND string_id = :string_id AND context = :context
SQL;

    private const SELECT_ROWS_BASE = <<<SQL
SELECT string_id, language_id, context, content, original_language,
       status, source_checksum, updated_by, date_updated
FROM strings
SQL;

    private PDO $pdo;

    public function __construct(string $path)
    {
        $this->pdo = new PDO('sqlite:' . $path);
        $this->pdo->setAttribute(PDO::ATTR_ERRMODE, PDO::ERRMODE_EXCEPTION);
        $this->pdo->exec('PRAGMA journal_mode = WAL');
        $this->pdo->exec('PRAGMA busy_timeout = 60000');
        $this->pdo->exec('PRAGMA foreign_keys = ON');
    }

    public function ensureSchema(): void
    {
        $this->pdo->exec(self::SCHEMA);
    }

    public function selectContent(string $stringId, string $languageId, string $context): ?string
    {
        $stmt = $this->pdo->prepare(self::SELECT);
        $stmt->execute(['language_id' => $languageId, 'string_id' => $stringId, 'context' => $context]);
        $content = $stmt->fetchColumn();
        return $content === false ? null : $content;
    }

    public function upsert(array $row): void
    {
        $stmt = $this->pdo->prepare(self::UPSERT);
        $stmt->execute([
            'language_id' => $row['language_id'],
            'string_id' => $row['string_id'],
            'context' => $row['context'],
            'content' => $row['content'],
            'original_language' => $row['original_language'],
            'status' => $row['status'],
            'source_checksum' => $row['source_checksum'],
            'updated_by' => $row['updated_by'],
            // SQLite has no native timestamp type; store ISO-8601 text,
            // same UTC instant every other backend's column represents.
            'date_updated' => $row['date_updated']->format('Y-m-d\TH:i:s.uP'),
        ]);
    }

    public function selectRows(?string $languageId, ?string $context, ?string $status): array
    {
        $clauses = [];
        $params = [];
        if ($languageId !== null) {
            $clauses[] = 'language_id = :language_id';
            $params['language_id'] = $languageId;
        }
        if ($context !== null) {
            $clauses[] = 'context = :context';
            $params['context'] = $context;
        }
        if ($status !== null) {
            $clauses[] = 'status = :status';
            $params['status'] = $status;
        }

        $sql = self::SELECT_ROWS_BASE . ($clauses ? ' WHERE ' . implode(' AND ', $clauses) : '');
        $stmt = $this->pdo->prepare($sql);
        $stmt->execute($params);

        $rows = [];
        while ($row = $stmt->fetch(PDO::FETCH_ASSOC)) {
            // SQLite has no native timestamp type; date_updated was
            // stored as text by upsert(), so it's parsed back here.
            $row['date_updated'] = new DateTimeImmutable($row['date_updated']);
            $rows[] = $row;
        }
        return $rows;
    }

    public function close(): void
    {
        // PDO closes the connection when the object is garbage collected;
        // there is no explicit close(). Dropping the reference is enough.
    }

    /** @internal exposed for tests that need to inspect raw rows */
    public function pdo(): PDO
    {
        return $this->pdo;
    }
}
