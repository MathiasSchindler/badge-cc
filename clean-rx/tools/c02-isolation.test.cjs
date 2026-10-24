'use strict';

const { test } = require('node:test');
const assert = require('node:assert/strict');
const { flagKey, resolverMakefile, inside } = require('./c02-isolation.cjs');

test('flag deduplication ignores ordering, not definitions', () => {
    assert.equal(flagKey('-DB=2 -DA=1'), flagKey('-DA=1 -DB=2'));
    assert.notEqual(flagKey('-DA=1'), flagKey('-DA=0'));
    assert.equal(flagKey(''), '');
    assert.throws(() => flagKey('-DA=1 -DA=2'), /Duplicate/);
    assert.throws(() => flagKey('-I../../src/shared'), /Unexpected/);
});

test('resolver retains Make evaluation but strips endpoint build/test leaves', () => {
    const result = resolverMakefile([
        'REF_FLAGS := -DCLEAN_RX_HT_RX=1',
        'REF_STIMULUS ?= 0',
        'reference: endpoint.uf2',
        'reference-alias:',
        '\t$(MAKE) -f $(REF_DIR)/Makefile REF_STIMULUS=1 reference reference-test',
        'reference-controller:',
        "\t$(MAKE) -C clean-rx/src EXTRA_CFLAGS='-DCLEAN_RX_HT_STIMULUS=$(REF_STIMULUS)' all test",
        'reference-test: native',
        '\tnative',
    ].join('\n'));
    assert.deepEqual(result.targets, ['reference', 'reference-alias']);
    assert.ok(result.text.includes('C02:observer:$(REF_FLAGS)'));
    assert.ok(result.text.includes('C02:stimulus:-DCLEAN_RX_HT_STIMULUS=$(REF_STIMULUS)'));
    assert.ok(result.text.includes('-f "$(C02_RESOLVER)"'));
    assert.ok(!result.text.includes('endpoint.uf2'));
    assert.ok(!result.text.includes('-C clean-rx/src'));
    assert.throws(() => resolverMakefile('reference-new:\n\tunknown'), /Unknown/);
});

test('standalone dependency containment rejects external and prefix-adjacent paths', () => {
    assert.ok(inside('/tmp/source', 'types.h'));
    assert.ok(inside('/tmp/source', '/tmp/source/bytes.h'));
    assert.ok(!inside('/tmp/source', '../../src/shared/stdint.h'));
    assert.ok(!inside('/tmp/source', '/tmp/source-other/types.h'));
});
