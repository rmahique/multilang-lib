<?php

declare(strict_types=1);

namespace Multilang\Backends;

use DateTimeImmutable;
use PDO;

/** PostgreSQL backend, via PDO. */
final class PostgresBackend implements BackendInterface
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
    date_updated      TIMESTAMPTZ NOT NULL,
    PRIMARY KEY (language_id, string_id, context)
)
SQL;

    private const UPSERT = <<<SQL
INSERT INTO strings
    (language_id, string_id, context, content, original_language,
     status, source_checksum, updated_by, date_updated)
VALUES (:language_id, :string_id, :context, :content, :original_language,
        :status, :source_checksum, :updated_by, :date_updated)
ON CONFLICT (language_id, string_id, context) DO UPDATE SET
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

    /**
     * @param bool $sslmode Defaults to false: the DSN then omits
     *   sslmode entirely, which libpq itself defaults to "prefer" (use
     *   TLS if the server offers it, don't fail if it doesn't). A plain
     *   localhost/container Postgres has no certificate to negotiate,
     *   and requiring TLS is an opt-in the caller makes explicitly (pass
     *   true, or set MULTILANG_DB_SSLMODE=require), not something forced
     *   on every connection.
     */
    public function __construct(
        string $host,
        int $port,
        string $user,
        string $password,
        string $database,
        bool $sslmode = false
    ) {
        // Credentials are passed as DSN/PDO connection parameters, never
        // interpolated into a query string or logged.
        $dsn = "pgsql:host=$host;port=$port;dbname=$database" . ($sslmode ? ';sslmode=require' : '');
        $this->pdo = new PDO($dsn, $user, $password);
        $this->pdo->setAttribute(PDO::ATTR_ERRMODE, PDO::ERRMODE_EXCEPTION);
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
            'date_updated' => $row['date_updated']->format('Y-m-d H:i:s.uP'),
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
            $row['date_updated'] = new DateTimeImmutable($row['date_updated']);
            $rows[] = $row;
        }
        return $rows;
    }

    public function close(): void
    {
    }

    /** @internal exposed for tests that need to inspect/reset raw rows */
    public function pdo(): PDO
    {
        return $this->pdo;
    }
}
