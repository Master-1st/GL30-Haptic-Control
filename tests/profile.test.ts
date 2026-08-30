import { test } from "node:test";
import assert from "node:assert/strict";
import { loadExampleProfiles, parseProfile } from "@gl30/profile";

test("example profiles are valid", () => {
  const examples = loadExampleProfiles();
  assert.equal(examples.length, 2);
  for (const profile of examples) {
    const result = parseProfile(profile);
    assert.equal(result.valid, true);
    assert.equal(result.errors.length, 0);
  }
});

test("invalid profile with extra property and semantic rule violations is rejected", () => {
  const examples = loadExampleProfiles();
  const example = examples[0];
  const withExtra = {
    ...(example as Record<string, unknown>),
    unexpectedExtra: true
  };
  const invalidExtra = parseProfile(withExtra);
  assert.equal(invalidExtra.valid, false);
  assert.equal(invalidExtra.errors.length > 0, true);

  const semantic = structuredClone(example as Record<string, unknown>);
  const haptic = semantic["haptic"] as Record<string, unknown>;
  const endstops = haptic["endstops"] as Record<string, unknown>;
  endstops["enabled"] = true;
  endstops["minPosition"] = 120;
  endstops["maxPosition"] = 120;

  const invalidSemantic = parseProfile(semantic);
  assert.equal(invalidSemantic.valid, false);
  assert.equal(invalidSemantic.errors.some((item) => item.path === "/haptic/endstops"), true);
  assert.equal((endstops["minPosition"] as number), 120);
  assert.equal((endstops["maxPosition"] as number), 120);
});
