'use strict';

const { test } = require('node:test');
const assert = require('node:assert/strict');
const validation = require('../src/validation');
const { ValidationError } = validation;

const VALID_LANGUAGE_IDS = [
  ['en', 'en'],
  ['es', 'es'],
  ['pt-BR', 'pt-br'],
  ['zh-Hans', 'zh-hans'],
  ['zh-Hans-CN', 'zh-hans-cn'],
  ['en-US', 'en-us'],
  ['sr-Latn-RS', 'sr-latn-rs'],
];

for (const [tag, expected] of VALID_LANGUAGE_IDS) {
  test(`valid language id ${tag} -> ${expected}`, () => {
    assert.equal(validation.validateLanguageId(tag), expected);
  });
}

const INVALID_LANGUAGE_IDS = ['', 'english', 'e', 'en_US', 'en--US', '123', null];
for (const tag of INVALID_LANGUAGE_IDS) {
  test(`invalid language id ${JSON.stringify(tag)}`, () => {
    assert.throws(() => validation.validateLanguageId(tag), ValidationError);
  });
}

test('optional language id: empty/null -> null', () => {
  assert.equal(validation.validateOptionalLanguageId(''), null);
  assert.equal(validation.validateOptionalLanguageId(null), null);
  assert.equal(validation.validateOptionalLanguageId(undefined), null);
});

const VALID_STRING_IDS = ['hello', 'button.publish', 'menu:item-42', 'a'.repeat(200)];
for (const sid of VALID_STRING_IDS) {
  test(`valid string id ${sid.slice(0, 20)}`, () => {
    assert.equal(validation.validateStringId(sid), sid);
  });
}

const INVALID_STRING_IDS = ['', 'a'.repeat(201), 'has space', 'has/slash', null, "quote'"];
for (const sid of INVALID_STRING_IDS) {
  test(`invalid string id ${JSON.stringify(sid)}`, () => {
    assert.throws(() => validation.validateStringId(sid), ValidationError);
  });
}

test('string id lowercased', () => {
  assert.equal(validation.validateStringId('Button.Publish'), 'button.publish');
});

test('context defaults to empty string', () => {
  assert.equal(validation.validateContext(null), '');
  assert.equal(validation.validateContext(undefined), '');
  assert.equal(validation.validateContext(''), '');
});

test('context lowercased', () => {
  assert.equal(validation.validateContext('Menu.Item'), 'menu.item');
});

test('content rejects NUL byte', () => {
  assert.throws(() => validation.validateContent('hello\x00world'), ValidationError);
});

test('content rejects empty', () => {
  assert.throws(() => validation.validateContent(''), ValidationError);
});

test('status allowlist', () => {
  for (const ok of ['draft', 'reviewed', 'published']) {
    assert.equal(validation.validateStatus(ok), ok);
  }
  assert.throws(() => validation.validateStatus('live'), ValidationError);
});
