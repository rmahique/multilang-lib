<?php

declare(strict_types=1);

namespace Multilang\Backends;

/**
 * Filesystem backend — no server, no driver, just files. Useful when the
 * translation set is meant to be human-editable and diffable in version
 * control rather than queried through a database.
 *
 * Layout (see ../../../docs/connectors.md#the-filesystem-backend):
 *
 *   <root>/<languageId>/<stringId>/<context>/content.json
 *   <root>/<languageId>/<stringId>/@default/content.json   (context === '')
 *
 * The leaf file is always named "content.json" — the row's data never
 * becomes part of a filename, only directory names (languageId, stringId,
 * context) do.
 */
final class FilesystemBackend implements BackendInterface
{
    private const DEFAULT_CONTEXT_DIR = '@default';
    private const CONTENT_FILENAME = 'content.json';

    private string $root;

    public function __construct(string $root)
    {
        $this->root = rtrim($root, '/');
    }

    public function ensureSchema(): void
    {
        if (!is_dir($this->root) && !mkdir($this->root, 0777, true) && !is_dir($this->root)) {
            throw new \RuntimeException("failed to create filesystem backend root: {$this->root}");
        }
    }

    /**
     * stringId/context allow "." and "-", so a value like ".." is a
     * valid identifier (see Validation.php) but a directory-traversal
     * payload as a path segment. Resolve and check containment before
     * touching disk, same spirit as every DB backend parameterizing its
     * queries instead of trusting input shape alone.
     */
    private function dirFor(string $languageId, string $stringId, string $context): string
    {
        $contextDir = $context === '' ? self::DEFAULT_CONTEXT_DIR : $context;
        $dir = $this->root . '/' . $languageId . '/' . $stringId . '/' . $contextDir;

        $rootReal = realpath($this->root) ?: $this->root;
        // The leaf directory may not exist yet, so resolve its parent
        // and rebuild instead of relying on realpath() of the full path.
        $resolved = $this->resolveLexically($dir);
        if ($resolved !== $rootReal && strpos($resolved, $rootReal . '/') !== 0) {
            throw new \RuntimeException(
                "refusing to access path outside the filesystem backend root for " .
                "languageId='$languageId' stringId='$stringId' context='$context'"
            );
        }
        return $dir;
    }

    /** Lexically collapse "." and ".." segments without requiring the path to exist. */
    private function resolveLexically(string $path): string
    {
        $isAbsolute = strncmp($path, '/', 1) === 0;
        $parts = [];
        foreach (explode('/', $path) as $segment) {
            if ($segment === '' || $segment === '.') {
                continue;
            }
            if ($segment === '..') {
                array_pop($parts);
                continue;
            }
            $parts[] = $segment;
        }
        return ($isAbsolute ? '/' : '') . implode('/', $parts);
    }

    public function selectContent(string $stringId, string $languageId, string $context): ?string
    {
        $file = $this->dirFor($languageId, $stringId, $context) . '/' . self::CONTENT_FILENAME;
        if (!is_file($file)) {
            return null;
        }
        $record = json_decode(file_get_contents($file), true, 512, JSON_THROW_ON_ERROR);
        return $record['content'];
    }

    public function upsert(array $row): void
    {
        $dir = $this->dirFor($row['language_id'], $row['string_id'], $row['context']);
        if (!is_dir($dir) && !mkdir($dir, 0777, true) && !is_dir($dir)) {
            throw new \RuntimeException("failed to create directory: $dir");
        }

        $record = [
            'content' => $row['content'],
            'original_language' => $row['original_language'],
            'status' => $row['status'],
            'source_checksum' => $row['source_checksum'],
            'updated_by' => $row['updated_by'],
            'date_updated' => $row['date_updated']->format('Y-m-d\TH:i:s.uP'),
        ];

        $file = $dir . '/' . self::CONTENT_FILENAME;
        $tmpFile = $file . '.tmp';
        file_put_contents($tmpFile, json_encode($record, JSON_PRETTY_PRINT | JSON_UNESCAPED_UNICODE) . "\n");
        // Atomic on POSIX for paths on the same filesystem, so a
        // concurrent reader never sees a partially written file.
        rename($tmpFile, $file);
    }

    public function close(): void
    {
        // No connection to close — files are opened and closed per call.
    }
}
