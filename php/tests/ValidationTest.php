<?php

declare(strict_types=1);

namespace Multilang\Tests;

use Multilang\Validation;
use Multilang\ValidationException;
use PHPUnit\Framework\TestCase;

final class ValidationTest extends TestCase
{
    /** @dataProvider validLanguageIds */
    public function testValidLanguageIds(string $tag, string $expected): void
    {
        $this->assertSame($expected, Validation::validateLanguageId($tag));
    }

    public static function validLanguageIds(): array
    {
        return [
            ['en', 'en'],
            ['es', 'es'],
            ['pt-BR', 'pt-br'],
            ['zh-Hans', 'zh-hans'],
            ['zh-Hans-CN', 'zh-hans-cn'],
            ['en-US', 'en-us'],
            ['sr-Latn-RS', 'sr-latn-rs'],
        ];
    }

    /** @dataProvider invalidLanguageIds */
    public function testInvalidLanguageIds($tag): void
    {
        $this->expectException(ValidationException::class);
        Validation::validateLanguageId($tag);
    }

    public static function invalidLanguageIds(): array
    {
        return [[''], ['english'], ['e'], ['en_US'], ['en--US'], ['123'], [null]];
    }

    public function testOptionalLanguageIdEmptyIsNull(): void
    {
        $this->assertNull(Validation::validateOptionalLanguageId(''));
        $this->assertNull(Validation::validateOptionalLanguageId(null));
    }

    /** @dataProvider validStringIds */
    public function testValidStringIds(string $sid): void
    {
        $this->assertSame($sid, Validation::validateStringId($sid));
    }

    public static function validStringIds(): array
    {
        return [['hello'], ['button.publish'], ['menu:item-42'], [str_repeat('a', 200)]];
    }

    /** @dataProvider invalidStringIds */
    public function testInvalidStringIds($sid): void
    {
        $this->expectException(ValidationException::class);
        Validation::validateStringId($sid);
    }

    public static function invalidStringIds(): array
    {
        return [[''], [str_repeat('a', 201)], ['has space'], ['has/slash'], [null], ["quote'"]];
    }

    public function testStringIdLowercased(): void
    {
        $this->assertSame('button.publish', Validation::validateStringId('Button.Publish'));
    }

    public function testContextDefaultsToEmptyString(): void
    {
        $this->assertSame('', Validation::validateContext(null));
        $this->assertSame('', Validation::validateContext(''));
    }

    public function testContextLowercased(): void
    {
        $this->assertSame('menu.item', Validation::validateContext('Menu.Item'));
    }

    public function testContentRejectsNulByte(): void
    {
        $this->expectException(ValidationException::class);
        Validation::validateContent("hello\x00world");
    }

    public function testContentRejectsEmpty(): void
    {
        $this->expectException(ValidationException::class);
        Validation::validateContent('');
    }

    public function testStatusAllowlist(): void
    {
        foreach (['draft', 'reviewed', 'published'] as $ok) {
            $this->assertSame($ok, Validation::validateStatus($ok));
        }
        $this->expectException(ValidationException::class);
        Validation::validateStatus('live');
    }

    /**
     * Regression test for a real bug: strtolower() is locale-sensitive.
     * Under a Turkish locale, strtolower('I') returns 'I' unchanged
     * instead of 'i' (Turkish distinguishes dotted/dotless i; strtolower()
     * can't represent the non-ASCII result in a single-byte string, so it
     * declines to convert). Every other language port lowercases 'I'
     * unconditionally, so if this ever regresses to locale-sensitive
     * lowering, a PHP process running under a Turkish (or similar) locale
     * would silently disagree with Python/JS/Go/C about whether "EN" and
     * "en" are the same language_id.
     */
    public function testLowercasingIsLocaleIndependent(): void
    {
        $restored = setlocale(LC_CTYPE, '0');
        if (setlocale(LC_CTYPE, 'tr_TR.utf8', 'tr_TR.UTF-8', 'tr_TR') === false) {
            $this->markTestSkipped('tr_TR locale not installed');
        }

        try {
            $result = Validation::validateLanguageId('EN-US');
            $this->assertSame('en-us', $result, "language_id must lowercase to 'en-us' regardless of process locale");
        } finally {
            setlocale(LC_CTYPE, $restored);
        }
    }
}
